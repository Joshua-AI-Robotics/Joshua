"""Inference node entry point.

This is a thin launcher. All ROS pub/sub orchestration lives in
``ai.runtime.host.InferenceHost`` and all model-specific behavior lives
in pluggable ``ai.runtime`` adapters resolved via the registry.
"""

import sys

from ai.runtime.host import InferenceHost
from ros2 import node_runner as node_runner_py


def main(argv=None):
    return node_runner_py.run_node(InferenceHost, logger_name="inference", argv=argv)


if __name__ == "__main__":
    sys.exit(main())
