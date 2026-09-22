#include "ros2/utils/mapped_message.h"

#include <cstdint>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

#include "rclcpp/serialization.hpp"
#include "rclcpp/typesupport_helpers.hpp"
#include "ros2/utils/checked_number.h"
#include "ros2/utils/numeric_message_config.h"
#include "rosidl_typesupport_introspection_cpp/field_types.hpp"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"
#include "utils/status_macros.h"

namespace ros2_utils {
namespace {
namespace introspection = rosidl_typesupport_introspection_cpp;
using introspection::MessageMember;
using introspection::MessageMembers;
struct Step {
  const MessageMember* member;
  std::optional<size_t> index;
};
using Path = std::vector<Step>;

absl::StatusOr<Path> CompilePath(const MessageMembers* members, const std::string& path) {
  Path result;
  size_t start = 0;
  while (start < path.size()) {
    const auto end = path.find('.', start);
    const auto token = path.substr(start, end == std::string::npos ? end : end - start);
    const auto bracket = token.find('[');
    const auto name = token.substr(0, bracket);
    const MessageMember* member = nullptr;
    for (size_t i = 0; i < members->member_count_; ++i) {
      if (name == members->members_[i].name_) member = &members->members_[i];
    }
    if (!member) return absl::InvalidArgumentError("Unknown field in path: " + path);
    std::optional<size_t> index;
    if (bracket != std::string::npos) {
      index = std::stoull(token.substr(bracket + 1, token.size() - bracket - 2));
      // Bound config-driven allocations, including unbounded ROS sequences.
      if (*index >= 4096) return absl::InvalidArgumentError("Field index must be below 4096");
    }
    if (member->is_array_ != index.has_value())
      return absl::InvalidArgumentError("Array fields require an explicit index: " + path);
    if (index && member->array_size_ > 0 && *index >= member->array_size_)
      return absl::InvalidArgumentError("Field index exceeds array bound: " + path);
    result.push_back({member, index});
    if (end == std::string::npos) break;
    if (member->type_id_ != introspection::ROS_TYPE_MESSAGE)
      return absl::InvalidArgumentError("Cannot traverse a scalar field: " + path);
    members = static_cast<const MessageMembers*>(member->members_->data);
    start = end + 1;
  }
  if (result.empty()) return absl::InvalidArgumentError("Empty field path");
  return result;
}

// The last array element uses fetch/assign callbacks (also handles vector<bool>).
struct Field {
  void* storage;
  const Step* step;
};
absl::StatusOr<Field> Resolve(void* message, const Path& path, bool write) {
  void* object = message;
  for (size_t i = 0; i < path.size(); ++i) {
    const auto& step = path[i];
    const auto& member = *step.member;
    void* storage = static_cast<char*>(object) + member.offset_;
    if (step.index) {
      if (!member.size_function) return absl::InvalidArgumentError("Missing array introspection");
      if (*step.index >= member.size_function(storage)) {
        if (!write || !member.resize_function)
          return absl::InvalidArgumentError("Message does not contain configured array element");
        member.resize_function(storage, *step.index + 1);
      }
    }
    if (i + 1 == path.size()) return Field{storage, &step};
    if (step.index) {
      if (!member.get_function) return absl::InvalidArgumentError("Cannot traverse array element");
      object = member.get_function(storage, *step.index);
    } else {
      object = storage;
    }
  }
  return absl::InternalError("Unreachable field resolution");
}

template <typename T>
T Read(const Field& field) {
  if (!field.step->index) return *static_cast<const T*>(field.storage);
  T value{};
  const auto& member = *field.step->member;
  if (member.fetch_function)
    member.fetch_function(field.storage, *field.step->index, &value);
  else if (member.get_const_function)
    value = *static_cast<const T*>(member.get_const_function(field.storage, *field.step->index));
  else
    throw std::runtime_error("Missing array read introspection");
  return value;
}
template <typename T>
void Write(const Field& field, const T& value) {
  if (!field.step->index) {
    *static_cast<T*>(field.storage) = value;
    return;
  }
  const auto& member = *field.step->member;
  if (member.assign_function)
    member.assign_function(field.storage, *field.step->index, &value);
  else if (member.get_function)
    *static_cast<T*>(member.get_function(field.storage, *field.step->index)) = value;
  else
    throw std::runtime_error("Missing array write introspection");
}

template <typename Fn>
absl::Status VisitNumber(uint8_t type, Fn fn) {
  using namespace introspection;
  switch (type) {
    case ROS_TYPE_FLOAT:
      return fn(float{});
    case ROS_TYPE_DOUBLE:
      return fn(double{});
    case ROS_TYPE_LONG_DOUBLE:
      return fn(static_cast<long double>(0));
    case ROS_TYPE_BOOLEAN:
      return fn(bool{});
    case ROS_TYPE_CHAR:
      return fn(uint8_t{});
    case ROS_TYPE_WCHAR:
      return fn(char16_t{});
    case ROS_TYPE_OCTET:
      return fn(uint8_t{});
    case ROS_TYPE_UINT8:
      return fn(uint8_t{});
    case ROS_TYPE_INT8:
      return fn(int8_t{});
    case ROS_TYPE_UINT16:
      return fn(uint16_t{});
    case ROS_TYPE_INT16:
      return fn(int16_t{});
    case ROS_TYPE_UINT32:
      return fn(uint32_t{});
    case ROS_TYPE_INT32:
      return fn(int32_t{});
    case ROS_TYPE_UINT64:
      return fn(uint64_t{});
    case ROS_TYPE_INT64:
      return fn(int64_t{});
    default:
      return absl::InvalidArgumentError("Mapped field must be numeric or boolean");
  }
}
absl::Status SetNumber(const Field& field, long double value) {
  return VisitNumber(field.step->member->type_id_, [&](auto zero) {
    using T = decltype(zero);
    if constexpr (std::is_same_v<T, bool>) {
      if (value != 0 && value != 1) return absl::OutOfRangeError("Boolean requires exactly 0 or 1");
    }
    ABSL_ASSIGN_OR_RETURN(auto converted, CheckedNumber<T>(value));
    Write(field, converted);
    return absl::OkStatus();
  });
}

class MessageStorage {
 public:
  explicit MessageStorage(const MessageMembers* members) : members_(members) {
    data_ = ::operator new(members_->size_of_);
    try {
      members_->init_function(data_, rosidl_runtime_cpp::MessageInitialization::ALL);
    } catch (...) {
      ::operator delete(data_);
      throw;
    }
  }
  ~MessageStorage() {
    members_->fini_function(data_);
    ::operator delete(data_);
  }
  void* data() {
    return data_;
  }

 private:
  const MessageMembers* members_;
  void* data_;
};
}  // namespace

struct MappedMessage::Impl {
  std::string type;
  ros2::node::ScalarMapping mapping;
  std::shared_ptr<rcpputils::SharedLibrary> introspection_library;
  std::shared_ptr<rcpputils::SharedLibrary> serialization_library;
  const MessageMembers* members;
  std::unique_ptr<rclcpp::SerializationBase> serialization;
  Path path;
  std::vector<Path> constants;
};
MappedMessage::MappedMessage(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
MappedMessage::~MappedMessage() = default;
const std::string& MappedMessage::type_name() const {
  return impl_->type;
}

absl::StatusOr<std::shared_ptr<MappedMessage>> MappedMessage::Create(
    ros2::data_type::Ros2DataType type, const ros2::node::ScalarMapping& mapping, bool publishing) {
  ABSL_RETURN_IF_ERROR(ValidateNumericMapping(type, mapping, publishing));
  try {
    auto impl = std::make_unique<Impl>();
    impl->type = RosMessageType(type);
    impl->mapping = mapping;
    impl->introspection_library =
        rclcpp::get_typesupport_library(impl->type, "rosidl_typesupport_introspection_cpp");
    const auto* handle = rclcpp::get_typesupport_handle(
        impl->type, "rosidl_typesupport_introspection_cpp", *impl->introspection_library);
    impl->members = static_cast<const MessageMembers*>(handle->data);
    impl->serialization_library =
        rclcpp::get_typesupport_library(impl->type, "rosidl_typesupport_cpp");
    impl->serialization =
        std::make_unique<rclcpp::SerializationBase>(rclcpp::get_typesupport_handle(
            impl->type, "rosidl_typesupport_cpp", *impl->serialization_library));
    const auto field_path = NumericFieldPath(type, mapping);
    ABSL_ASSIGN_OR_RETURN(impl->path, CompilePath(impl->members, field_path));
    ABSL_RETURN_IF_ERROR(
        VisitNumber(impl->path.back().member->type_id_, [](auto) { return absl::OkStatus(); }));
    std::set<std::string> used{field_path};
    MessageStorage probe(impl->members);
    for (const auto& constant : mapping.constants()) {
      if (!used.insert(constant.field_path()).second)
        return absl::InvalidArgumentError("Duplicate or overlapping mapped field: " +
                                          constant.field_path());
      ABSL_ASSIGN_OR_RETURN(auto path, CompilePath(impl->members, constant.field_path()));
      ABSL_ASSIGN_OR_RETURN(auto field, Resolve(probe.data(), path, true));
      if (constant.has_text()) {
        if (path.back().member->type_id_ != introspection::ROS_TYPE_STRING)
          return absl::InvalidArgumentError("Text constant requires a string field");
        if (path.back().member->string_upper_bound_ &&
            constant.text().size() > path.back().member->string_upper_bound_)
          return absl::InvalidArgumentError("Text constant exceeds field bound");
        Write(field, constant.text());
      } else {
        ABSL_RETURN_IF_ERROR(SetNumber(field, constant.number()));
      }
      impl->constants.push_back(std::move(path));
    }
    return std::shared_ptr<MappedMessage>(new MappedMessage(std::move(impl)));
  } catch (const std::exception& error) {
    return absl::InvalidArgumentError(std::string("Cannot initialize ROS mapping: ") +
                                      error.what());
  }
}

absl::StatusOr<rclcpp::SerializedMessage> MappedMessage::Encode(float value) const {
  try {
    MessageStorage message(impl_->members);
    for (size_t i = 0; i < impl_->constants.size(); ++i) {
      const auto& constant = impl_->mapping.constants(i);
      ABSL_ASSIGN_OR_RETURN(auto field, Resolve(message.data(), impl_->constants[i], true));
      if (constant.has_text())
        Write(field, constant.text());
      else {
        ABSL_RETURN_IF_ERROR(SetNumber(field, constant.number()));
      }
    }
    ABSL_ASSIGN_OR_RETURN(auto field, Resolve(message.data(), impl_->path, true));
    const auto& mapping = impl_->mapping;
    const long double mapped =
        static_cast<long double>(value) * (mapping.has_scale() ? mapping.scale() : 1.0L) +
        mapping.offset();
    ABSL_RETURN_IF_ERROR(SetNumber(field, mapped));
    rclcpp::SerializedMessage serialized;
    impl_->serialization->serialize_message(message.data(), &serialized);
    return serialized;
  } catch (const std::exception& error) {
    return absl::InvalidArgumentError(error.what());
  }
}
absl::StatusOr<float> MappedMessage::Decode(const rclcpp::SerializedMessage& serialized) const {
  try {
    MessageStorage message(impl_->members);
    impl_->serialization->deserialize_message(&serialized, message.data());
    ABSL_ASSIGN_OR_RETURN(auto field, Resolve(message.data(), impl_->path, false));
    float result = 0;
    ABSL_RETURN_IF_ERROR(VisitNumber(field.step->member->type_id_, [&](auto zero) {
      using T = decltype(zero);
      const long double value = Read<T>(field);
      const auto& mapping = impl_->mapping;
      const long double mapped =
          value * (mapping.has_scale() ? mapping.scale() : 1.0L) + mapping.offset();
      ABSL_ASSIGN_OR_RETURN(result, CheckedNumber<float>(mapped));
      if constexpr (std::is_integral_v<T>) {
        if (static_cast<long double>(result) != mapped)
          return absl::OutOfRangeError(
              "Integer command cannot be represented exactly by float driver API");
      }
      return absl::OkStatus();
    }));
    return result;
  } catch (const std::exception& error) {
    return absl::InvalidArgumentError(error.what());
  }
}
}  // namespace ros2_utils
