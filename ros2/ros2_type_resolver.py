from typing import Any
import importlib

from ros2.proto import ros2_data_type_pb2


def resolve_message_class(ros2_type: str, enum_value: int) -> Any:
    """
    Resolve ROS 2 message class from either a fully-qualified type string
    (e.g., "sensor_msgs/msg/Image") or a Ros2DataType enum value.
    """
    if ros2_type:
        return _resolve_from_string(ros2_type)
    return _resolve_from_enum(enum_value)


def resolve_message_class_from_enum(enum_value: int) -> Any:
    """Resolve ROS 2 message class strictly from Ros2DataType enum."""
    return _resolve_from_enum(enum_value)


def _resolve_from_string(ros2_type: str) -> Any:
    try:
        return _import_message_class(ros2_type)
    except Exception as exc:
        raise ValueError(f"Failed to resolve ROS 2 type '{ros2_type}': {exc}")


def _resolve_from_enum(enum_value: int) -> Any:
    try:
        name = ros2_data_type_pb2.Ros2DataType.Name(enum_value)
    except Exception:
        raise ValueError(f"Unknown Ros2DataType value: {enum_value}")

    normalized = name.upper()
    for prefix in ("DATA_TYPE_", "ROS2_DATA_TYPE_"):
        if normalized.startswith(prefix):
            normalized = normalized[len(prefix):]

    # This mapping data MUST match Ros2DataType enum values in ai_model.proto.
    mapping = {
        # std_msgs
        "FLOAT32": "std_msgs/msg/Float32",
        "FLOAT64": "std_msgs/msg/Float64",
        "INT32": "std_msgs/msg/Int32",
        "BOOL": "std_msgs/msg/Bool",
        "STRING": "std_msgs/msg/String",
        "INT8": "std_msgs/msg/Int8",
        "INT16": "std_msgs/msg/Int16",
        "INT64": "std_msgs/msg/Int64",
        "UINT8": "std_msgs/msg/UInt8",
        "UINT16": "std_msgs/msg/UInt16",
        "UINT32": "std_msgs/msg/UInt32",
        "UINT64": "std_msgs/msg/UInt64",
        "BYTE": "std_msgs/msg/Byte",
        "CHAR": "std_msgs/msg/Char",
        "EMPTY": "std_msgs/msg/Empty",
        "COLOR_RGBA": "std_msgs/msg/ColorRGBA",
        "HEADER": "std_msgs/msg/Header",
        "FLOAT32_MULTI_ARRAY": "std_msgs/msg/Float32MultiArray",
        "FLOAT64_MULTI_ARRAY": "std_msgs/msg/Float64MultiArray",
        "INT8_MULTI_ARRAY": "std_msgs/msg/Int8MultiArray",
        "INT16_MULTI_ARRAY": "std_msgs/msg/Int16MultiArray",
        "INT32_MULTI_ARRAY": "std_msgs/msg/Int32MultiArray",
        "INT64_MULTI_ARRAY": "std_msgs/msg/Int64MultiArray",
        "UINT8_MULTI_ARRAY": "std_msgs/msg/UInt8MultiArray",
        "UINT16_MULTI_ARRAY": "std_msgs/msg/UInt16MultiArray",
        "UINT32_MULTI_ARRAY": "std_msgs/msg/UInt32MultiArray",
        "UINT64_MULTI_ARRAY": "std_msgs/msg/UInt64MultiArray",
        "MULTI_ARRAY_DIMENSION": "std_msgs/msg/MultiArrayDimension",
        "MULTI_ARRAY_LAYOUT": "std_msgs/msg/MultiArrayLayout",
        # sensor_msgs
        "IMAGE": "sensor_msgs/msg/Image",
        "LASER_SCAN": "sensor_msgs/msg/LaserScan",
        "IMU": "sensor_msgs/msg/Imu",
        "POINTCLOUD2": "sensor_msgs/msg/PointCloud2",
        "BATTERY_STATE": "sensor_msgs/msg/BatteryState",
        "CAMERA_INFO": "sensor_msgs/msg/CameraInfo",
        "CHANNEL_FLOAT32": "sensor_msgs/msg/ChannelFloat32",
        "COMPRESSED_IMAGE": "sensor_msgs/msg/CompressedImage",
        "FLUID_PRESSURE": "sensor_msgs/msg/FluidPressure",
        "ILLUMINANCE": "sensor_msgs/msg/Illuminance",
        "JOINT_STATE": "sensor_msgs/msg/JointState",
        "JOY": "sensor_msgs/msg/Joy",
        "JOY_FEEDBACK": "sensor_msgs/msg/JoyFeedback",
        "JOY_FEEDBACK_ARRAY": "sensor_msgs/msg/JoyFeedbackArray",
        "LASER_ECHO": "sensor_msgs/msg/LaserEcho",
        "MAGNETIC_FIELD": "sensor_msgs/msg/MagneticField",
        "MULTI_DOF_JOINT_STATE": "sensor_msgs/msg/MultiDOFJointState",
        "MULTI_ECHO_LASER_SCAN": "sensor_msgs/msg/MultiEchoLaserScan",
        "NAV_SAT_FIX": "sensor_msgs/msg/NavSatFix",
        "NAV_SAT_STATUS": "sensor_msgs/msg/NavSatStatus",
        "POINT_CLOUD": "sensor_msgs/msg/PointCloud",
        "POINT_FIELD": "sensor_msgs/msg/PointField",
        "RANGE": "sensor_msgs/msg/Range",
        "REGION_OF_INTEREST": "sensor_msgs/msg/RegionOfInterest",
        "RELATIVE_HUMIDITY": "sensor_msgs/msg/RelativeHumidity",
        "TEMPERATURE": "sensor_msgs/msg/Temperature",
        "TIME_REFERENCE": "sensor_msgs/msg/TimeReference",
        # nav_msgs
        "ODOMETRY": "nav_msgs/msg/Odometry",
        "GRID_CELLS": "nav_msgs/msg/GridCells",
        "MAP_META_DATA": "nav_msgs/msg/MapMetaData",
        "OCCUPANCY_GRID": "nav_msgs/msg/OccupancyGrid",
        "PATH": "nav_msgs/msg/Path",
        # geometry_msgs
        "TWIST": "geometry_msgs/msg/Twist",
        "POSE": "geometry_msgs/msg/Pose",
        "ACCEL": "geometry_msgs/msg/Accel",
        "ACCEL_STAMPED": "geometry_msgs/msg/AccelStamped",
        "ACCEL_WITH_COVARIANCE": "geometry_msgs/msg/AccelWithCovariance",
        "ACCEL_WITH_COVARIANCE_STAMPED": "geometry_msgs/msg/AccelWithCovarianceStamped",
        "INERTIA": "geometry_msgs/msg/Inertia",
        "INERTIA_STAMPED": "geometry_msgs/msg/InertiaStamped",
        "POINT": "geometry_msgs/msg/Point",
        "POINT32": "geometry_msgs/msg/Point32",
        "POINT_STAMPED": "geometry_msgs/msg/PointStamped",
        "POLYGON": "geometry_msgs/msg/Polygon",
        "POLYGON_STAMPED": "geometry_msgs/msg/PolygonStamped",
        "POSE2D": "geometry_msgs/msg/Pose2D",
        "POSE_ARRAY": "geometry_msgs/msg/PoseArray",
        "POSE_STAMPED": "geometry_msgs/msg/PoseStamped",
        "POSE_WITH_COVARIANCE": "geometry_msgs/msg/PoseWithCovariance",
        "POSE_WITH_COVARIANCE_STAMPED": "geometry_msgs/msg/PoseWithCovarianceStamped",
        "QUATERNION": "geometry_msgs/msg/Quaternion",
        "QUATERNION_STAMPED": "geometry_msgs/msg/QuaternionStamped",
        "TRANSFORM": "geometry_msgs/msg/Transform",
        "TRANSFORM_STAMPED": "geometry_msgs/msg/TransformStamped",
        "TWIST_STAMPED": "geometry_msgs/msg/TwistStamped",
        "TWIST_WITH_COVARIANCE": "geometry_msgs/msg/TwistWithCovariance",
        "TWIST_WITH_COVARIANCE_STAMPED": "geometry_msgs/msg/TwistWithCovarianceStamped",
        "VECTOR3": "geometry_msgs/msg/Vector3",
        "VECTOR3_STAMPED": "geometry_msgs/msg/Vector3Stamped",
        "WRENCH": "geometry_msgs/msg/Wrench",
        "WRENCH_STAMPED": "geometry_msgs/msg/WrenchStamped",
        # tf2_msgs
        "TF_MESSAGE": "tf2_msgs/msg/TFMessage",
    }

    if normalized not in mapping:
        raise ValueError(f"Unsupported Ros2DataType: {name}")
    return _import_message_class(mapping[normalized])


def _import_message_class(ros2_type: str) -> Any:
    """
    Import a ROS 2 interface class using Python import mechanics.
    Accepts strings like "package/msg/Type", "package/srv/Type", or "package/action/Type".
    """
    parts = ros2_type.split("/")
    if len(parts) != 3:
        raise ValueError(
            f"Invalid ROS 2 type format '{ros2_type}'. Expected 'package/(msg|srv|action)/Type'."
        )

    package, interface_kind, type_name = parts
    if interface_kind not in ("msg", "srv", "action"):
        raise ValueError(
            f"Invalid interface kind '{interface_kind}' in '{ros2_type}'. Expected 'msg', 'srv', or 'action'."
        )

    module_name = f"{package}.{interface_kind}"
    try:
        module = importlib.import_module(module_name)
    except ModuleNotFoundError as exc:
        raise ImportError(
            f"Could not import module '{module_name}' for '{ros2_type}': {exc}"
        ) from exc

    try:
        return getattr(module, type_name)
    except AttributeError as exc:
        raise ImportError(
            f"Type '{type_name}' not found in module '{module_name}' for '{ros2_type}'."
        ) from exc


