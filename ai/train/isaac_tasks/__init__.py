"""Joshua Isaac Lab task & terms library.

Importing this package adds ``ai/train/isaac_tasks`` to ``sys.path``
and triggers gym registration for all Joshua tasks.

Typical usage (inside ``isaac_runner.py``)::

    import isaac_tasks          # triggers gym.register()
    from isaac_tasks import terms  # term factory functions
"""

import importlib
import os
import pkgutil
import sys

_PACKAGE_DIR = os.path.dirname(os.path.abspath(__file__))
_PARENT_DIR = os.path.dirname(_PACKAGE_DIR)
if _PARENT_DIR not in sys.path:
    sys.path.insert(0, _PARENT_DIR)

from isaac_tasks import terms  # noqa: E402, F401

_tasks_pkg = importlib.import_module("isaac_tasks.tasks")
for _info in pkgutil.iter_modules(_tasks_pkg.__path__):
    importlib.import_module(f"isaac_tasks.tasks.{_info.name}")
