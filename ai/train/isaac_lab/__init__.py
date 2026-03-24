"""Joshua Isaac Lab integration library.

Importing this package adds ``ai/train/isaac_lab`` to ``sys.path``
and exposes reusable MDP term factories.

Typical usage (inside ``isaac_runner.py``)::

    import isaac_lab
    from isaac_lab import terms
"""

import os
import sys

_PACKAGE_DIR = os.path.dirname(os.path.abspath(__file__))
_PARENT_DIR = os.path.dirname(_PACKAGE_DIR)
if _PARENT_DIR not in sys.path:
    sys.path.insert(0, _PARENT_DIR)

from isaac_lab import terms  # noqa: E402, F401
