"""C++ helper adapter; subprocess lifecycle is shared with validation-only mode."""

import json

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
            "enum": ["list_supported_components", "list_devices", "describe_device"],
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

    async def aclose(self):
        await self.service.close()
