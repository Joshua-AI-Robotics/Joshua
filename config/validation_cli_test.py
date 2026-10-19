import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

BINARY = (
    Path(os.environ["TEST_SRCDIR"])
    / os.environ["TEST_WORKSPACE"]
    / "config/joshua_validate_config"
)
FIELDS = {
    "schema_version",
    "scope",
    "config_path",
    "stage",
    "result",
    "status_code",
    "message",
    "limitations",
}


class ValidationCliTest(unittest.TestCase):
    def run_validator(self, *args, input_path=None):
        completed = subprocess.run([str(BINARY), *args], capture_output=True)
        if input_path is None:
            return completed, None
        self.assertEqual(completed.stdout.count(b"\n"), 1)
        report = json.loads(completed.stdout)
        self.assertEqual(set(report), FIELDS)
        self.assertEqual(report["config_path"], str(input_path))
        self.assertEqual(report["scope"], "config_integrity")
        self.assertEqual(report["schema_version"], 1)
        return completed, report

    def test_empty_and_malformed(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "config.pbtxt"
            path.write_text("")
            process, report = self.run_validator("--config", str(path), input_path=path)
            self.assertEqual(process.returncode, 0)
            self.assertEqual(
                (report["stage"], report["result"], report["status_code"]),
                ("integrity", "valid", "OK"),
            )
            path.write_text("unknown_field: 1")
            process, report = self.run_validator("--config", str(path), input_path=path)
            self.assertEqual(process.returncode, 1)
            self.assertEqual((report["stage"], report["result"]), ("load", "invalid"))

    def test_missing_and_nonregular_inputs(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "missing.pbtxt"
            process, report = self.run_validator("--config", str(path), input_path=path)
            self.assertEqual(
                (process.returncode, report["stage"], report["result"]),
                (1, "input", "not_run"),
            )
            process, report = self.run_validator("--config", temp, input_path=temp)
            self.assertEqual((process.returncode, report["stage"]), (1, "input"))
            process, report = self.run_validator(
                "--config", "/dev/null", input_path="/dev/null"
            )
            self.assertEqual((process.returncode, report["stage"]), (1, "input"))
            process, report = self.run_validator(
                "--config", "/proc/self/cmdline", input_path="/proc/self/cmdline"
            )
            self.assertEqual((process.returncode, report["stage"]), (1, "input"))
            fifo = Path(temp) / "fifo"
            os.mkfifo(fifo)
            process, report = self.run_validator("--config", str(fifo), input_path=fifo)
            self.assertEqual((process.returncode, report["stage"]), (1, "input"))

    def test_usage_and_symlink(self):
        process, report = self.run_validator("--unknown")
        self.assertEqual(process.returncode, 2)
        self.assertEqual(json.loads(process.stdout)["stage"], "arguments")
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp) / "target.pbtxt"
            target.write_text("")
            link = Path(temp) / "link.pbtxt"
            link.symlink_to(target)
            process, report = self.run_validator(
                "--config=" + str(link), input_path=link
            )
            self.assertEqual((process.returncode, report["result"]), (0, "valid"))

    def test_integrity_references_and_declared_nonexistent_device(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "config.pbtxt"
            path.write_text("""
robot {
  perceptions { single_perceptions {
    node { id: 1 node_type: POSITION_PUBLISHER }
    sensor_name: "joint"
    sensor_type: POSITION
    sts3215_encoder_config { board_name: "missing" channel: 1 }
  }}
}
""")
            process, report = self.run_validator("--config", str(path), input_path=path)
            self.assertEqual(
                (
                    process.returncode,
                    report["stage"],
                    report["result"],
                    report["status_code"],
                ),
                (1, "integrity", "invalid", "NOT_FOUND"),
            )
            path.write_text("""
robot {
  perceptions { single_perceptions {
    node { id: 1 node_type: LIDAR_PUBLISHER }
    sensor_name: "scan"
    sensor_type: RANGE_SCAN
    lds01_config { comm {
      comm_type: SERIAL
      transport_type: BYTE_STREAM
      serial_config { port: "/dev/joshua-nonexistent-robot" baudrate: 230400 }
    }}
  }}
}
""")
            process, report = self.run_validator("--config", str(path), input_path=path)
            self.assertEqual((process.returncode, report["result"]), (0, "valid"))

    def test_bus_conflict(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "conflict.pbtxt"
            path.write_text("""
robot {
  boards {
    name: "bus"
    board_type: FEETECH_BUS
    comm { comm_type: SERIAL transport_type: MESSAGE
      serial_config { port: "/dev/joshua-nonexistent-bus" baudrate: 1000000 }}
    channels { index: 1 drive: SERVO_BUS_UART servo_bus { servo_id: 1 }}
  }
  perceptions {
    single_perceptions {
      node { id: 1 node_type: POSITION_PUBLISHER }
      sensor_name: "one" sensor_type: POSITION
      sts3215_encoder_config { board_name: "bus" channel: 1 }
    }
    single_perceptions {
      node { id: 2 node_type: POSITION_PUBLISHER }
      sensor_name: "two" sensor_type: POSITION
      sts3215_encoder_config { board_name: "bus" channel: 1 }
    }
  }
}
""")
            process, report = self.run_validator("--config", str(path), input_path=path)
            self.assertEqual(
                (process.returncode, report["stage"], report["status_code"]),
                (1, "integrity", "INVALID_ARGUMENT"),
            )

    def test_usage_invalid_utf8_and_stderr_only_diagnostics(self):
        for args in [(), ("--config",), ("--config=a", "--config=b"), ("--other",)]:
            with self.subTest(args=args):
                process, _ = self.run_validator(*args)
                self.assertEqual(
                    (process.returncode, json.loads(process.stdout)["stage"]),
                    (2, "arguments"),
                )
        help_result, _ = self.run_validator("--help")
        self.assertEqual(help_result.returncode, 0)
        self.assertIn(b"Usage:", help_result.stdout)
        bad_path = subprocess.run(
            [os.fsencode(str(BINARY)), b"--config", b"/tmp/\xff"], capture_output=True
        )
        self.assertEqual(
            (bad_path.returncode, json.loads(bad_path.stdout)["stage"]), (1, "input")
        )
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "bad.pbtxt"
            path.write_text("unknown_field: 1")
            process, _ = self.run_validator("--config", str(path), input_path=path)
            self.assertTrue(process.stderr)
            self.assertEqual(process.stdout.count(b"\n"), 1)

    def test_dependency_graph_excludes_runtime_and_hardware(self):
        graph = (
            (
                Path(os.environ["TEST_SRCDIR"])
                / os.environ["TEST_WORKSPACE"]
                / "config/validation_dependency_graph"
            )
            .read_text()
            .splitlines()
        )
        self.assertIn("//config:validation", graph)
        for label in graph:
            with self.subTest(label=label):
                self.assertFalse(
                    label.startswith(
                        ("//launcher:", "//node_generator:", "//firmware:")
                    )
                )
                if label.startswith("//simulation:"):
                    self.fail("validator depends on simulation runtime")
                if label.startswith("//robot/board/factory:"):
                    self.assertIn(
                        label,
                        {
                            "//robot/board/factory:board_resolver",
                            "//robot/board/factory:board_resolver.cc",
                            "//robot/board/factory:board_resolver.h",
                        },
                    )
                if label.startswith("//robot/comm/") and "/proto:" not in label:
                    self.fail(
                        "validator depends on hardware communication implementation"
                    )
                if label.startswith("//robot/action/") and "/proto:" not in label:
                    self.fail("validator depends on action implementation")

    def test_size_boundary_and_escaping(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'a"\tconfig.pbtxt'
            path.write_bytes(b" " * (10 * 1024 * 1024))
            process, report = self.run_validator("--config", str(path), input_path=path)
            self.assertEqual((process.returncode, report["result"]), (0, "valid"))
            with path.open("ab") as stream:
                stream.write(b" ")
            process, report = self.run_validator("--config", str(path), input_path=path)
            self.assertEqual((process.returncode, report["stage"]), (1, "input"))
            self.assertEqual(process.stdout.count(b"\n"), 1)


if __name__ == "__main__":
    unittest.main()
