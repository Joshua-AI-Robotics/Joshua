"""Post-launcher inference entry point (runs inside the model environment)."""

from __future__ import annotations

import sys
from typing import List, Optional

from ros2.utils.ros_python_paths import setup_import_paths

setup_import_paths()

from ai.inference.host import InferenceHost  # noqa: E402
from ros2 import node_runner as node_runner_py  # noqa: E402


def main(argv: Optional[List[str]] = None) -> int:
    return node_runner_py.run_node(InferenceHost, logger_name="inference", argv=argv)


if __name__ == "__main__":
    sys.exit(main())
