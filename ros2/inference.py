"""Inference node entry point.

Thin wrapper: resolves the model from config and re-execs into the model's
isolated environment when required. ROS orchestration lives in
``ai.runtime.host.InferenceHost``; model behavior lives in adapters.
"""

import sys

from ai.runtime.inference_launcher import main as inference_launcher_main


def main(argv=None):
    return inference_launcher_main(argv)


if __name__ == "__main__":
    sys.exit(main())
