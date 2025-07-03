import sys
import os

# Add protobuf paths to ensure google module is found
# TODO: Update the project setting to avoid this manual dependency check.
if hasattr(sys, '_getframe'):
    # Running under Bazel - add runfiles protobuf paths
    current_dir = os.path.dirname(os.path.abspath(__file__))
    runfiles_root = None
    path_parts = current_dir.split(os.sep)
    for i, part in enumerate(path_parts):
        if part.endswith('.runfiles'):
            runfiles_root = os.sep.join(path_parts[:i+1])
            break
    
    if runfiles_root:
        dependency_paths = [
            os.path.join(runfiles_root, 'protobuf+', 'python'),
            os.path.join(runfiles_root, 'rules_python++pip+project_joshua_pip_deps_310_protobuf', 'site-packages'),
            os.path.join(runfiles_root, 'rules_python++pip+project_joshua_pip_deps_310_numpy', 'site-packages')
        ]
        for path in dependency_paths:
            if os.path.exists(path) and path not in sys.path:
                sys.path.insert(0, path)

import numpy as np

class BaseAiLayer:
    def __init__(self):
        pass

    def process(self, input_data):
        pass