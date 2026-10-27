#include "ros2/camera_image.h"

#include <limits>
namespace ros2_utils {
absl::StatusOr<sensor_msgs::msg::Image> CameraPacketImage(
    const robot::perception::PerceptionPacket& packet) {
  if (!packet.has_image()) return absl::InvalidArgumentError("Image packet required.");
  const auto& image = packet.image();
  if (image.width() <= 0 || image.height() <= 0 || image.channels() != 3 ||
      (image.encoding() != "bgr8" && image.encoding() != "rgb8"))
    return absl::InvalidArgumentError(
        "Require RGB8/BGR8 with three channels and positive dimensions.");
  const uint64_t row = static_cast<uint64_t>(image.width()) * 3;
  if (row > std::numeric_limits<uint32_t>::max() ||
      static_cast<uint64_t>(image.height()) > std::numeric_limits<size_t>::max() / row)
    return absl::InvalidArgumentError("Image layout arithmetic overflow.");
  const size_t length = static_cast<size_t>(row) * image.height();
  if (image.data().size() != length)
    return absl::InvalidArgumentError("Image packet byte length differs from layout.");
  sensor_msgs::msg::Image result;
  result.width = image.width();
  result.height = image.height();
  result.encoding = "rgb8";
  result.step = static_cast<uint32_t>(row);
  result.data.assign(image.data().begin(), image.data().end());
  if (image.encoding() == "bgr8")
    for (size_t i = 0; i < length; i += 3) std::swap(result.data[i], result.data[i + 2]);
  return result;
}
}  // namespace ros2_utils
