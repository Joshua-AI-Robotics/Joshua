#include <glog/logging.h>

#include <chrono>

#include "rclcpp/rclcpp.hpp"
#include "ros2/proto/node.pb.h"

namespace ros2_utils {

// Return QoS object from QosSetting proto message at 'ros2/proto/node.proto'.
inline rclcpp::QoS CreateQosSetting(const ros2::node::QosSetting& qos_setting) {
  rclcpp::QoS qos_profile(rclcpp::QoSInitialization::from_rmw(rmw_qos_profile_default));

  // 1. Reliability
  switch (qos_setting.reliability_policy()) {
    case ros2::node::QosReliabilityPolicy::QOS_RELIABILITY_POLICY_RELIABLE:
      qos_profile.reliability(RMW_QOS_POLICY_RELIABILITY_RELIABLE);
      break;
    case ros2::node::QosReliabilityPolicy::QOS_RELIABILITY_POLICY_BEST_EFFORT:
      qos_profile.reliability(RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT);
      break;
    default:
      LOG(WARNING) << "Invalid reliability policy: "
                   << ros2::node::QosReliabilityPolicy_Name(qos_setting.reliability_policy())
                   << ". Using default reliable policy.";
      qos_profile.reliability(RMW_QOS_POLICY_RELIABILITY_RELIABLE);
      break;
  }

  // 2. Durability
  switch (qos_setting.durability_policy()) {
    case ros2::node::QosDurabilityPolicy::QOS_DURABILITY_POLICY_TRANSIENT_LOCAL:
      qos_profile.durability(RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL);
      break;
    case ros2::node::QosDurabilityPolicy::QOS_DURABILITY_POLICY_VOLATILE:
      qos_profile.durability(RMW_QOS_POLICY_DURABILITY_VOLATILE);
      break;
    default:
      LOG(WARNING) << "Invalid durability policy: "
                   << ros2::node::QosDurabilityPolicy_Name(qos_setting.durability_policy())
                   << ". Using default volatile policy.";
      qos_profile.durability(RMW_QOS_POLICY_DURABILITY_VOLATILE);
      break;
  }

  // 3. History
  switch (qos_setting.history_policy()) {
    case ros2::node::QosHistoryPolicy::QOS_HISTORY_POLICY_KEEP_LAST:
      qos_profile.history(RMW_QOS_POLICY_HISTORY_KEEP_LAST);
      break;
    case ros2::node::QosHistoryPolicy::QOS_HISTORY_POLICY_KEEP_ALL:
      qos_profile.history(RMW_QOS_POLICY_HISTORY_KEEP_ALL);
      break;
    default:
      LOG(WARNING) << "Invalid history policy: "
                   << ros2::node::QosHistoryPolicy_Name(qos_setting.history_policy())
                   << ". Using default keep last policy.";
      qos_profile.history(RMW_QOS_POLICY_HISTORY_KEEP_LAST);
      break;
  }

  // 4. Depth
  if (qos_setting.depth() > 0) {
    qos_profile.depth(qos_setting.depth());
  } else {
    LOG(WARNING) << "Depth is not set. Using default depth 10.";
    qos_profile.depth(10);
  }

  // 5. Liveliness
  switch (qos_setting.liveliness_policy()) {
    case ros2::node::QosLivelinessPolicy::QOS_LIVELINESS_POLICY_AUTOMATIC:
      qos_profile.liveliness(RMW_QOS_POLICY_LIVELINESS_AUTOMATIC);
      break;
    case ros2::node::QosLivelinessPolicy::QOS_LIVELINESS_POLICY_MANUAL_BY_TOPIC:
      qos_profile.liveliness(RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC);
      break;
    default:
      LOG(WARNING) << "Invalid liveliness policy: "
                   << ros2::node::QosLivelinessPolicy_Name(qos_setting.liveliness_policy())
                   << ". Using default liveliness policy.";
      qos_profile.liveliness(RMW_QOS_POLICY_LIVELINESS_AUTOMATIC);
      break;
  }

  // 6. Liveliness Lease Duration.
  if (qos_setting.liveliness_lease_duration_nanoseconds() > 0) {
    qos_profile.lease_duration(
        std::chrono::nanoseconds(qos_setting.liveliness_lease_duration_nanoseconds()));
  }

  // 7. Deadline, default is infinite.
  if (qos_setting.deadline_nanoseconds() > 0) {
    qos_profile.deadline(std::chrono::nanoseconds(qos_setting.deadline_nanoseconds()));
  }
  // 8. Lifespan, default is infinite.
  if (qos_setting.lifespan_nanoseconds() > 0) {
    qos_profile.lifespan(std::chrono::nanoseconds(qos_setting.lifespan_nanoseconds()));
  }

  return qos_profile;
}
}  // namespace ros2_utils
