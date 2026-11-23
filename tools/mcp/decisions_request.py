import asyncio
import secrets
import time
from collections import deque

from openai import APIError, APITimeoutError

from ai.models.decisions.choice import decode_choice

ROUTES = {
    "answer": "Explain a concept or answer a general question about robotics.",
    "status": "Read observed runtime status, including which processes are running.",
    "clarify": "Ask for the missing object or intent of an ambiguous request.",
    "unsupported": "The user requests a capability the system does not offer.",
}


class DecisionsRequests:
    def __init__(self, api, observer, deadline=20, replay=None):
        self.api = api
        self.observer = observer
        self.deadline = deadline
        self.history = deque(maxlen=4)
        self.sequence = 0
        self.last = None
        self.active = None
        self.replay = replay
        self.stopped_through = 0
        self.pending = None

    def stop(self, sequence):
        if type(sequence) is not int or sequence < 1:
            raise ValueError("Use a positive integer sequence.")
        if sequence == self.sequence:
            return "completed"
        if sequence != self.sequence + 1:
            raise ValueError("Unknown turn.")
        if self.pending and "replay_command" in self.pending:
            return "completed"
        self.stopped_through = sequence
        if self.active:
            self.active.cancel()
        return "stopped"

    def bind_replay(self, replay):
        if self.active:
            self.stop(self.sequence + 1)
        self.replay = replay

    async def request(self, text, sequence):
        if type(text) is not str or not 0 < len(text.strip()) <= 2000:
            raise ValueError("Enter between 1 and 2000 characters.")
        if type(sequence) is not int or sequence < 1:
            raise ValueError("Use a positive integer turn sequence.")
        text = text.strip()
        if sequence == self.sequence and self.last and self.last[0] == text:
            return self.last[1]
        if sequence != self.sequence + 1 or self.active:
            raise ValueError("Turn is out of sequence or already in progress.")
        report = {
            "turn_id": secrets.token_hex(12),
            "sequence": sequence,
            "route": None,
            "outcome": "completed",
        }
        started = time.monotonic()
        replay = self.replay
        deadline = started + self.deadline

        def admissible():
            return (
                self.pending is report
                and self.replay is replay
                and sequence > self.stopped_through
                and time.monotonic() < deadline
            )

        self.pending = report
        try:
            if sequence <= self.stopped_through:
                raise asyncio.CancelledError()
            self.active = asyncio.create_task(
                self.dispatch(text, report, replay, admissible)
            )
            report["reply"] = await asyncio.wait_for(
                asyncio.shield(self.active), self.deadline
            )
            if not admissible() and "replay_command" not in report:
                raise asyncio.CancelledError()
        except (asyncio.TimeoutError, APITimeoutError):
            self.stop(sequence)
            report.update(outcome="timeout", reply="The request timed out.")
        except asyncio.CancelledError:
            stopped = sequence <= self.stopped_through
            self.stop(sequence)
            report.update(outcome="cancelled", reply="The request was cancelled.")
            if not stopped:
                raise
        except (APIError, ValueError, TypeError, KeyError, AttributeError):
            report.update(
                outcome="failed", reply="The provider returned no usable result."
            )
        finally:
            if "replay_command" in report:
                cmd = report["replay_command"]
                report.update(
                    outcome="completed",
                    reply=f"Replay recorded one {cmd['joint']} target at "
                    f"{cmd['radians']:+.3f} rad. No hardware moved.",
                )
            if self.active and not self.active.done():
                self.active.add_done_callback(
                    lambda t: None if t.cancelled() else t.exception()
                )
            self.pending = None
            report["elapsed_ms"] = round((time.monotonic() - started) * 1000)
            self.active = None
            self.sequence = sequence
            self.last = text, dict(report)
            turn = {"message": text, "route": report["route"]}
            if report["route"] in {"answer", "clarify", "unsupported"} or (
                report["route"] == "status" and report["outcome"] == "completed"
            ):
                turn["reply"] = report.get("reply", "")[:1000]
            self.history.append(turn)
        return self.last[1]

    async def dispatch(self, text, report, replay, admissible):
        name = "operator_intent_" + report["turn_id"]
        routes = dict(ROUTES)
        instructions = (
            "Which capability best handles the current operator message? "
            "Use history only to resolve a reference. Choose among the listed options."
        )
        target = None
        if replay is not None:
            routes["scene"] = (
                "Describe, locate or count objects seen in the camera image."
            )
            offer_head, target = replay.motion_target(text)
            if offer_head:
                routes["head"] = (
                    "Point the robot camera at the object named by the user."
                )
            instructions += (
                " The robot has a camera and a movable head. A request to look at a "
                "named object asks for a head turn. A question about what or where "
                "something is asks for a visual answer. An unnamed motion target "
                "needs clarification. Grasping is unavailable."
            )
        parts = [
            {"type": "input_json", "data": {"previous_turns": list(self.history)}},
            {"type": "input_text", "text": text},
        ]
        started = time.monotonic()
        raw = await self.api.decisions.with_raw_response.create(
            model="gpt-6-luna",
            input=[{"role": "user", "content": parts}],
            questions=[
                {
                    "type": "choice",
                    "name": name,
                    "instructions": instructions,
                    "choices": [
                        {"value": k, "description": v} for k, v in routes.items()
                    ],
                }
            ],
        )
        if not admissible():
            raise asyncio.CancelledError()
        decision = raw.http_response.json()
        route_info = {
            "ms": round((time.monotonic() - started) * 1000),
            "request_id": raw.headers.get("x-request-id"),
            "tokens": (decision.get("usage") or {}).get("total_tokens"),
        }
        report["routing"] = route_info
        if decision.get("model") != "gpt-6-luna":
            raise ValueError("Unexpected model.")
        (answer,) = decision["answers"]
        try:
            route = decode_choice(name, routes, answer, 0.8)
        except ValueError as exc:
            if str(exc) == "refused":
                report["outcome"] = "refused"
                return "The routing request was refused."
            if str(exc) != "low_confidence":
                raise
            report["outcome"] = "low_confidence"
            route = "clarify"
        route_info.update(candidate=answer["choice"], confidence=answer["confidence"])
        report["route"] = route
        if route == "clarify":
            return "What would you like Joshua to do, and which target do you mean?"
        if route == "unsupported":
            return "That capability is unavailable in this Joshua session."
        if route == "head":
            if target is None:
                target = await replay.extract_target(text, report, self.api)
            if not admissible():
                raise asyncio.CancelledError()
            if target is None:
                report["outcome"] = "unbound_target"
                return "Which object should the replay head turn toward?"
            report["config_id"] = replay.config_id
            return await replay.step(target, report, self.api, admissible)
        if route == "status":
            observed = await self.observer.get_runtime_status()
            report["observation"] = {
                k: observed[k]
                for k in (
                    "availability",
                    "instance_id",
                    "snapshot_sequence",
                    "snapshot_age_seconds",
                    "status_code",
                )
            }
            if observed["availability"] != "fresh":
                report["outcome"] = observed["availability"]
                return "Current runtime status is unavailable. " + observed["message"]
            data = observed["data"]
            names = [p["node_name"] for p in data["processes"][:20]]
            processes = ", ".join(names) if names else "none"
            if data["tracked_process_count"] > len(names):
                processes += f" (first {len(names)} of {data['tracked_process_count']})"
            return (
                f"Runtime phase: {data['phase']}. "
                f"Observed processes: {processes}. "
                f"Snapshot age: {observed['snapshot_age_seconds']:.2f} s."
            )
        started = time.monotonic()
        messages = []
        for turn in self.history:
            messages.append({"role": "user", "content": turn["message"]})
            if turn.get("reply"):
                messages.append({"role": "assistant", "content": turn["reply"]})
        messages.append({"role": "user", "content": text})
        instructions = (
            "Answer the current question. Joshua supports these routes: "
            + ", ".join(routes)
            + ". You have no tools or observations of current robot state."
        )
        if route == "scene":
            instructions += (
                " Answer only the specific visual question using the synthetic replay "
                "frame. Identify it as replay. Omit incidental details and speculation."
            )
            messages[-1]["content"] = [
                {"type": "input_text", "text": text},
                {"type": "input_image", "image_url": replay.image_url},
            ]
        response = await self.api.responses.create(
            model="gpt-4.1-mini-2025-04-14",
            instructions=instructions,
            input=messages,
            store=False,
            max_output_tokens=384,
        )
        report["answer"] = {
            "ms": round((time.monotonic() - started) * 1000),
            "request_id": response._request_id,
            "tokens": getattr(response.usage, "total_tokens", None),
        }
        if response.status != "completed" or not response.output_text.strip():
            report["outcome"] = "unanswered"
            return "The answer provider could not complete this reply."
        return response.output_text.strip()
