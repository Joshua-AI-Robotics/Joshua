"""Keep C++ wire type names synchronized with Python and the protobuf enum."""

import ast
import pathlib
import re
import unittest

from ros2.proto import ros2_data_type_pb2


class TypeMappingTest(unittest.TestCase):
    def test_cpp_python_and_proto_agree(self):
        root = pathlib.Path(__file__).resolve().parents[2]
        tree = ast.parse((root / "ros2/ros2_type_resolver.py").read_text())
        mapping = next(
            ast.literal_eval(node.value)
            for node in tree.body
            if isinstance(node, ast.Assign)
            and any(
                isinstance(t, ast.Name) and t.id == "ROS2_TYPE_MAPPING"
                for t in node.targets
            )
        )
        source = (root / "ros2/utils/numeric_message_config.cc").read_text()
        cpp = dict(
            re.findall(r'case ros2::data_type::(\w+):\s*return "([^"]+)";', source)
        )
        self.assertEqual(mapping, cpp)
        self.assertEqual(
            set(mapping),
            set(ros2_data_type_pb2.Ros2DataType.keys()) - {"DATA_TYPE_INVALID"},
        )


if __name__ == "__main__":
    unittest.main()
