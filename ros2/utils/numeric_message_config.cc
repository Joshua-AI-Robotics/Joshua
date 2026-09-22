#include "ros2/utils/numeric_message_config.h"

#include <cmath>
#include <regex>
namespace ros2_utils {
std::string RosMessageType(ros2::data_type::Ros2DataType type) {
  // Keep in sync with ROS2_TYPE_MAPPING; tested by type_mapping_test.py.
  switch (type) {
    case ros2::data_type::FLOAT32:
      return "std_msgs/msg/Float32";
    case ros2::data_type::FLOAT64:
      return "std_msgs/msg/Float64";
    case ros2::data_type::INT32:
      return "std_msgs/msg/Int32";
    case ros2::data_type::BOOL:
      return "std_msgs/msg/Bool";
    case ros2::data_type::STRING:
      return "std_msgs/msg/String";
    case ros2::data_type::INT8:
      return "std_msgs/msg/Int8";
    case ros2::data_type::INT16:
      return "std_msgs/msg/Int16";
    case ros2::data_type::INT64:
      return "std_msgs/msg/Int64";
    case ros2::data_type::UINT8:
      return "std_msgs/msg/UInt8";
    case ros2::data_type::UINT16:
      return "std_msgs/msg/UInt16";
    case ros2::data_type::UINT32:
      return "std_msgs/msg/UInt32";
    case ros2::data_type::UINT64:
      return "std_msgs/msg/UInt64";
    case ros2::data_type::BYTE:
      return "std_msgs/msg/Byte";
    case ros2::data_type::CHAR:
      return "std_msgs/msg/Char";
    case ros2::data_type::EMPTY:
      return "std_msgs/msg/Empty";
    case ros2::data_type::COLOR_RGBA:
      return "std_msgs/msg/ColorRGBA";
    case ros2::data_type::HEADER:
      return "std_msgs/msg/Header";
    case ros2::data_type::FLOAT32_MULTI_ARRAY:
      return "std_msgs/msg/Float32MultiArray";
    case ros2::data_type::FLOAT64_MULTI_ARRAY:
      return "std_msgs/msg/Float64MultiArray";
    case ros2::data_type::INT8_MULTI_ARRAY:
      return "std_msgs/msg/Int8MultiArray";
    case ros2::data_type::INT16_MULTI_ARRAY:
      return "std_msgs/msg/Int16MultiArray";
    case ros2::data_type::INT32_MULTI_ARRAY:
      return "std_msgs/msg/Int32MultiArray";
    case ros2::data_type::INT64_MULTI_ARRAY:
      return "std_msgs/msg/Int64MultiArray";
    case ros2::data_type::UINT8_MULTI_ARRAY:
      return "std_msgs/msg/UInt8MultiArray";
    case ros2::data_type::UINT16_MULTI_ARRAY:
      return "std_msgs/msg/UInt16MultiArray";
    case ros2::data_type::UINT32_MULTI_ARRAY:
      return "std_msgs/msg/UInt32MultiArray";
    case ros2::data_type::UINT64_MULTI_ARRAY:
      return "std_msgs/msg/UInt64MultiArray";
    case ros2::data_type::MULTI_ARRAY_DIMENSION:
      return "std_msgs/msg/MultiArrayDimension";
    case ros2::data_type::MULTI_ARRAY_LAYOUT:
      return "std_msgs/msg/MultiArrayLayout";
    case ros2::data_type::IMAGE:
      return "sensor_msgs/msg/Image";
    case ros2::data_type::LASER_SCAN:
      return "sensor_msgs/msg/LaserScan";
    case ros2::data_type::IMU:
      return "sensor_msgs/msg/Imu";
    case ros2::data_type::POINTCLOUD2:
      return "sensor_msgs/msg/PointCloud2";
    case ros2::data_type::BATTERY_STATE:
      return "sensor_msgs/msg/BatteryState";
    case ros2::data_type::CAMERA_INFO:
      return "sensor_msgs/msg/CameraInfo";
    case ros2::data_type::CHANNEL_FLOAT32:
      return "sensor_msgs/msg/ChannelFloat32";
    case ros2::data_type::COMPRESSED_IMAGE:
      return "sensor_msgs/msg/CompressedImage";
    case ros2::data_type::FLUID_PRESSURE:
      return "sensor_msgs/msg/FluidPressure";
    case ros2::data_type::ILLUMINANCE:
      return "sensor_msgs/msg/Illuminance";
    case ros2::data_type::JOINT_STATE:
      return "sensor_msgs/msg/JointState";
    case ros2::data_type::JOY:
      return "sensor_msgs/msg/Joy";
    case ros2::data_type::JOY_FEEDBACK:
      return "sensor_msgs/msg/JoyFeedback";
    case ros2::data_type::JOY_FEEDBACK_ARRAY:
      return "sensor_msgs/msg/JoyFeedbackArray";
    case ros2::data_type::LASER_ECHO:
      return "sensor_msgs/msg/LaserEcho";
    case ros2::data_type::MAGNETIC_FIELD:
      return "sensor_msgs/msg/MagneticField";
    case ros2::data_type::MULTI_DOF_JOINT_STATE:
      return "sensor_msgs/msg/MultiDOFJointState";
    case ros2::data_type::MULTI_ECHO_LASER_SCAN:
      return "sensor_msgs/msg/MultiEchoLaserScan";
    case ros2::data_type::NAV_SAT_FIX:
      return "sensor_msgs/msg/NavSatFix";
    case ros2::data_type::NAV_SAT_STATUS:
      return "sensor_msgs/msg/NavSatStatus";
    case ros2::data_type::POINT_CLOUD:
      return "sensor_msgs/msg/PointCloud";
    case ros2::data_type::POINT_FIELD:
      return "sensor_msgs/msg/PointField";
    case ros2::data_type::RANGE:
      return "sensor_msgs/msg/Range";
    case ros2::data_type::REGION_OF_INTEREST:
      return "sensor_msgs/msg/RegionOfInterest";
    case ros2::data_type::RELATIVE_HUMIDITY:
      return "sensor_msgs/msg/RelativeHumidity";
    case ros2::data_type::TEMPERATURE:
      return "sensor_msgs/msg/Temperature";
    case ros2::data_type::TIME_REFERENCE:
      return "sensor_msgs/msg/TimeReference";
    case ros2::data_type::ODOMETRY:
      return "nav_msgs/msg/Odometry";
    case ros2::data_type::GRID_CELLS:
      return "nav_msgs/msg/GridCells";
    case ros2::data_type::MAP_META_DATA:
      return "nav_msgs/msg/MapMetaData";
    case ros2::data_type::OCCUPANCY_GRID:
      return "nav_msgs/msg/OccupancyGrid";
    case ros2::data_type::PATH:
      return "nav_msgs/msg/Path";
    case ros2::data_type::TWIST:
      return "geometry_msgs/msg/Twist";
    case ros2::data_type::POSE:
      return "geometry_msgs/msg/Pose";
    case ros2::data_type::ACCEL:
      return "geometry_msgs/msg/Accel";
    case ros2::data_type::ACCEL_STAMPED:
      return "geometry_msgs/msg/AccelStamped";
    case ros2::data_type::ACCEL_WITH_COVARIANCE:
      return "geometry_msgs/msg/AccelWithCovariance";
    case ros2::data_type::ACCEL_WITH_COVARIANCE_STAMPED:
      return "geometry_msgs/msg/AccelWithCovarianceStamped";
    case ros2::data_type::INERTIA:
      return "geometry_msgs/msg/Inertia";
    case ros2::data_type::INERTIA_STAMPED:
      return "geometry_msgs/msg/InertiaStamped";
    case ros2::data_type::POINT:
      return "geometry_msgs/msg/Point";
    case ros2::data_type::POINT32:
      return "geometry_msgs/msg/Point32";
    case ros2::data_type::POINT_STAMPED:
      return "geometry_msgs/msg/PointStamped";
    case ros2::data_type::POLYGON:
      return "geometry_msgs/msg/Polygon";
    case ros2::data_type::POLYGON_STAMPED:
      return "geometry_msgs/msg/PolygonStamped";
    case ros2::data_type::POSE2D:
      return "geometry_msgs/msg/Pose2D";
    case ros2::data_type::POSE_ARRAY:
      return "geometry_msgs/msg/PoseArray";
    case ros2::data_type::POSE_STAMPED:
      return "geometry_msgs/msg/PoseStamped";
    case ros2::data_type::POSE_WITH_COVARIANCE:
      return "geometry_msgs/msg/PoseWithCovariance";
    case ros2::data_type::POSE_WITH_COVARIANCE_STAMPED:
      return "geometry_msgs/msg/PoseWithCovarianceStamped";
    case ros2::data_type::QUATERNION:
      return "geometry_msgs/msg/Quaternion";
    case ros2::data_type::QUATERNION_STAMPED:
      return "geometry_msgs/msg/QuaternionStamped";
    case ros2::data_type::TRANSFORM:
      return "geometry_msgs/msg/Transform";
    case ros2::data_type::TRANSFORM_STAMPED:
      return "geometry_msgs/msg/TransformStamped";
    case ros2::data_type::TWIST_STAMPED:
      return "geometry_msgs/msg/TwistStamped";
    case ros2::data_type::TWIST_WITH_COVARIANCE:
      return "geometry_msgs/msg/TwistWithCovariance";
    case ros2::data_type::TWIST_WITH_COVARIANCE_STAMPED:
      return "geometry_msgs/msg/TwistWithCovarianceStamped";
    case ros2::data_type::VECTOR3:
      return "geometry_msgs/msg/Vector3";
    case ros2::data_type::VECTOR3_STAMPED:
      return "geometry_msgs/msg/Vector3Stamped";
    case ros2::data_type::WRENCH:
      return "geometry_msgs/msg/Wrench";
    case ros2::data_type::WRENCH_STAMPED:
      return "geometry_msgs/msg/WrenchStamped";
    case ros2::data_type::TF_MESSAGE:
      return "tf2_msgs/msg/TFMessage";
    default:
      return "";
  }
}
std::string NumericFieldPath(ros2::data_type::Ros2DataType type,
                             const ros2::node::ScalarMapping& mapping) {
  if (mapping.field_path().empty() && type == ros2::data_type::FLOAT32) return "data";
  return mapping.field_path();
}
absl::Status ValidateNumericMapping(ros2::data_type::Ros2DataType type,
                                    const ros2::node::ScalarMapping& mapping,
                                    bool publishing) {
  if (RosMessageType(type).empty()) return absl::InvalidArgumentError("Unknown ROS message type");
  static const std::regex path(
      R"([a-zA-Z_][a-zA-Z_0-9]*(\[[0-9]+\])?(\.[a-zA-Z_][a-zA-Z_0-9]*(\[[0-9]+\])?)*)");
  if (!std::regex_match(NumericFieldPath(type, mapping), path))
    return absl::InvalidArgumentError(
        "Explicit numeric field_path required (e.g. data, position[0], linear.x)");
  if ((mapping.has_scale() && (!std::isfinite(mapping.scale()) || mapping.scale() == 0)) ||
      !std::isfinite(mapping.offset()))
    return absl::InvalidArgumentError(
        "Scalar mapping requires finite nonzero scale and finite offset");
  if (!publishing && !mapping.constants().empty())
    return absl::InvalidArgumentError("Field constants are only valid for publishers");
  for (const auto& constant : mapping.constants()) {
    if (!std::regex_match(constant.field_path(), path) ||
        constant.value_case() == ros2::node::FieldConstant::VALUE_NOT_SET ||
        (constant.has_number() && !std::isfinite(constant.number())))
      return absl::InvalidArgumentError("Invalid publisher field constant");
  }
  return absl::OkStatus();
}
}  // namespace ros2_utils
