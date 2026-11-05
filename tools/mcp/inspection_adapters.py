"""C++ helper adapter; subprocess lifecycle is shared with validation-only mode."""

import json
import math

from tools.mcp.config_validation_server import (
    STATUS_CODES,
    _unique_pairs,
    safe_reference,
)
from tools.mcp.interfaces import SetupInspector

SETUP_LIMITATIONS = [
    (
        "Installed software selection is not physical compatibility "
        "or hardware readiness. No creator, driver, cache, ROS or "
        "device calls were made. Inventory omits entries beyond 128 "
        "or identities beyond 512 UTF-8 bytes; describe searches the "
        "complete candidate."
    )
]
SETUP_KEYS = {
    "schema_version",
    "scope",
    "operation",
    "config_path",
    "stage",
    "status_code",
    "message",
    "data",
    "limitations",
}


SETUP_SCHEMA = {
    "type": "object",
    "properties": {
        "schema_version": {"type": "integer", "const": 1},
        "scope": {"type": "string", "const": "setup_inspection"},
        "operation": {
            "type": "string",
            "enum": [
                "list_supported_components",
                "list_devices",
                "describe_device",
                "describe_robot_capabilities",
            ],
        },
        "config_path": {"type": ["string", "null"]},
        "stage": {
            "type": "string",
            "enum": [
                "arguments",
                "input",
                "load",
                "integrity",
                "inspection",
                "adapter",
            ],
        },
        "status_code": {"type": "string", "enum": sorted(STATUS_CODES)},
        "message": {"type": "string"},
        "data": {"type": "object"},
        "limitations": {"type": "array", "items": {"type": "string"}},
    },
    "required": sorted(SETUP_KEYS),
    "additionalProperties": False,
}


def setup_error(operation, path, code, message):
    return dict(
        schema_version=1,
        scope="setup_inspection",
        operation=operation,
        config_path=path,
        stage="adapter",
        status_code=code,
        message=message,
        data={},
        limitations=list(SETUP_LIMITATIONS),
    )


def validate_setup_report(report, operation, path, exit_code=None):
    if type(report) is not dict or set(report) != SETUP_KEYS:
        raise ValueError("wrong inspection fields")
    if type(report["schema_version"]) is not int or report["schema_version"] != 1:
        raise ValueError("wrong inspection version")
    if (
        report["scope"] != "setup_inspection"
        or report["operation"] != operation
        or report["config_path"] != path
    ):
        raise ValueError("wrong inspection identity")
    if (
        report["stage"]
        not in {"arguments", "input", "load", "integrity", "inspection", "adapter"}
        or report["status_code"] not in STATUS_CODES
    ):
        raise ValueError("wrong inspection status")
    if (
        type(report["message"]) is not str
        or len(report["message"].encode("utf-8")) > 8192
        or report["limitations"] != SETUP_LIMITATIONS
        or type(report["data"]) is not dict
    ):
        raise ValueError("wrong inspection data")
    if exit_code is not None and (
        (report["status_code"] == "OK") != (exit_code == 0) or exit_code not in (0, 1)
    ):
        raise ValueError("inconsistent inspection exit")
    data = report["data"]
    if report["status_code"] == "OK":
        if report["stage"] != "inspection":
            raise ValueError("wrong completed inspection stage")
        if operation == "list_supported_components":
            if set(data) != {
                "boards",
                "communication",
                "motors",
                "sensors",
                "build_version",
                "support_evidence",
                "integration_guide",
            }:
                raise ValueError("wrong catalog fields")
            for key in ("boards", "communication", "motors", "sensors"):
                if type(data[key]) is not list or len(data[key]) > 128:
                    raise ValueError("wrong catalog collection")
                for item in data[key]:
                    fields = {"name", "support_scope"} | (
                        {"transport", "configuration_constraints"}
                        if key == "communication"
                        else {"sensor_config"} if key == "sensors" else set()
                    )
                    if (
                        type(item) is not dict
                        or set(item) != fields
                        or any(type(v) is not str for v in item.values())
                        or item["support_scope"]
                        not in {"installed", "test_only", "demo_only", "unimplemented"}
                    ):
                        raise ValueError("wrong factory descriptor")
            if any(
                type(data[k]) is not str
                for k in ("build_version", "support_evidence", "integration_guide")
            ):
                raise ValueError("wrong catalog provenance")
        elif operation == "list_devices":
            if (
                set(data)
                != {"devices", "omitted_devices", "build_version", "config_name"}
                or type(data["devices"]) is not list
                or len(data["devices"]) > 128
                or type(data["omitted_devices"]) is not int
                or data["omitted_devices"] < 0
                or type(data["build_version"]) is not str
                or type(data["config_name"]) is not str
            ):
                raise ValueError("wrong inventory")
            for device in data["devices"]:
                _validate_device(device)
        elif operation == "describe_robot_capabilities":
            _validate_capabilities(data)
        else:
            if set(data) != {"device"}:
                raise ValueError("wrong description")
            _validate_device(data["device"])
    elif data:
        raise ValueError("failed inspection must not have data")
    return report


def _validate_device(device):
    if (
        type(device) is not dict
        or set(device)
        != {
            "device_id",
            "kind",
            "name",
            "configured",
            "hardware_readiness",
            "compatibility",
        }
        or device["kind"] not in {"board", "actuator", "sensor"}
        or type(device["name"]) is not str
        or device["device_id"] != device["kind"] + ":" + device["name"]
        or type(device["configured"]) is not dict
        or device["hardware_readiness"] != "unknown"
        or device["compatibility"] != "not_verified_by_inspection"
    ):
        raise ValueError("wrong device description")


def _validate_capabilities(data):
    if set(data) != {
        "config_name",
        "build_version",
        "hardware_readiness",
        "operations",
        "gaps",
        "prerequisites",
        "evidence",
    }:
        raise ValueError("wrong capability fields")
    if (
        type(data["config_name"]) is not str
        or type(data["build_version"]) is not str
        or data["hardware_readiness"] != "unknown"
    ):
        raise ValueError("wrong capability identity")
    for key in ("prerequisites", "evidence"):
        if (
            type(data[key]) is not list
            or not data[key]
            or any(type(v) is not str for v in data[key])
        ):
            raise ValueError("wrong capability evidence")
    if type(data["gaps"]) is not list:
        raise ValueError("wrong capability gaps")
    for gap in data["gaps"]:
        if (
            type(gap) is not dict
            or set(gap) != {"target", "reason"}
            or any(type(v) is not str for v in gap.values())
        ):
            raise ValueError("wrong capability gap")
    if type(data["operations"]) is not list or len(data["operations"]) != 2:
        raise ValueError("wrong capability operations")
    for op, name in zip(
        data["operations"], ("joint_position_target", "joint_position_observation")
    ):
        if (
            type(op) is not dict
            or set(op)
            != {
                "name",
                "parameters",
                "driver_contract",
                "verification",
                "mcp_execution",
                "targets",
            }
            or op["name"] != name
            or op["mcp_execution"] != "unavailable"
            or type(op["parameters"]) is not str
            or type(op["driver_contract"]) is not str
            or type(op["verification"]) is not str
            or not op["verification"]
            or type(op["targets"]) is not list
        ):
            raise ValueError("wrong capability operation")
        for target in op["targets"]:
            keys = {
                "device_id",
                "board_name",
                "channel",
                "units",
                "reference",
                "configured_ros",
            }
            keys |= (
                {
                    "operational_lower_limit",
                    "operational_upper_limit",
                    "physical_lower_limit",
                    "physical_upper_limit",
                    "idle_position",
                    "feedback_sensor_ids",
                    "ros_exposure",
                }
                if name == "joint_position_target"
                else {"mcp_feedback"}
            )
            if (
                type(target) is not dict
                or set(target) != keys
                or any(
                    type(target[k]) is not str
                    for k in ("device_id", "board_name", "reference")
                )
                or not target["device_id"].startswith(
                    "actuator:" if name == "joint_position_target" else "sensor:"
                )
                or type(target["channel"]) is not int
                or target["channel"] < 0
                or target["units"] != "native_ticks"
                or type(target["configured_ros"]) is not list
            ):
                raise ValueError("wrong capability target")
            if name == "joint_position_target":
                if target["ros_exposure"] not in {"configured", "not_evidenced"} or (
                    target["ros_exposure"] == "configured"
                ) != bool(target["configured_ros"]):
                    raise ValueError("wrong capability exposure")
                for key in (
                    "operational_lower_limit",
                    "operational_upper_limit",
                    "physical_lower_limit",
                    "physical_upper_limit",
                    "idle_position",
                ):
                    if type(target[key]) not in (int, float) or not math.isfinite(
                        target[key]
                    ):
                        raise ValueError("wrong capability limits")
                if type(target["feedback_sensor_ids"]) is not list or any(
                    type(v) is not str or not v.startswith("sensor:")
                    for v in target["feedback_sensor_ids"]
                ):
                    raise ValueError("wrong capability feedback association")
            elif target["mcp_feedback"] not in {
                "requires_operator_preflight_selection_and_live_publisher_verification",
                "not_configured_for_get_feedback",
            }:
                raise ValueError("wrong capability feedback availability")
            for endpoint in target["configured_ros"]:
                if (
                    type(endpoint) is not dict
                    or set(endpoint) != {"topic", "message_type", "command", "units"}
                    or any(type(v) is not str for v in endpoint.values())
                    or endpoint["units"] not in {"native_ticks", "radians"}
                ):
                    raise ValueError("wrong capability ROS endpoint")


class CppSetupInspector(SetupInspector):
    def __init__(self, service, inspector=None):
        self.service = service
        self.inspector = inspector

    async def validate_config(self, config_path):
        return await self.service.validate(config_path)

    async def _inspect(self, operation, config_path=None, device_id=None):
        path = config_path if safe_reference(config_path) else None
        if operation != "list_supported_components" and path is None:
            return setup_error(
                operation,
                None,
                "INVALID_ARGUMENT",
                "Require one safe relative config_path.",
            )
        if device_id is not None and (
            type(device_id) is not str
            or not device_id
            or len(device_id.encode("utf-8")) > 512
        ):
            return setup_error(
                operation,
                path,
                "INVALID_ARGUMENT",
                "Require a bounded configured device_id.",
            )
        if not self.inspector:
            return setup_error(
                operation,
                path,
                "UNAVAILABLE",
                "Operator must configure --inspector for setup discovery.",
            )

        def parse(raw, exit_code, expected):
            if not raw.endswith(b"\n") or raw.count(b"\n") != 1:
                raise ValueError("expected one inspection JSON line")
            result = json.loads(raw.decode("utf-8"), object_pairs_hook=_unique_pairs)
            return validate_setup_report(result, operation, expected, exit_code)

        args = ("--operation", operation) + (
            ("--device-id", device_id) if device_id else ()
        )
        report = await self.service.run_helper(
            path, self.inspector, args, parse, operation == "list_supported_components"
        )
        if report.get("scope") == "config_integrity":
            return setup_error(
                operation, path, report["status_code"], report["message"]
            )
        return report

    async def list_supported_components(self):
        return await self._inspect("list_supported_components")

    async def list_devices(self, config_path):
        return await self._inspect("list_devices", config_path)

    async def describe_device(self, config_path, device_id):
        return await self._inspect("describe_device", config_path, device_id)

    async def describe_robot_capabilities(self, config_path):
        return await self._inspect("describe_robot_capabilities", config_path)

    async def aclose(self):
        await self.service.close()
