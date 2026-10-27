// Test-only codec callback changes the derived binding after real JPEG encoding.
#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <opencv2/imgcodecs.hpp>
#include <string>

#include "google/protobuf/struct.pb.h"
#include "google/protobuf/util/json_util.h"
#include "ros2/image_observer_backend.h"
int main(int argc, char** argv) {
  if (argc != 5) return 2;
#if defined(JOSHUA_TEST_CODEC_FAILURE)
  return ros2_utils::RunImageObserver(
      argc, argv, [](const cv::Mat&, std::vector<uint8_t>&) { return false; });
#elif defined(JOSHUA_TEST_CODEC_OVERSIZE)
  return ros2_utils::RunImageObserver(argc, argv, [](const cv::Mat&, std::vector<uint8_t>& bytes) {
    bytes.assign(512 * 1024 + 1, 0);
    return true;
  });
#else
  int directory = std::stoi(argv[2]);
  return ros2_utils::RunImageObserver(
      argc, argv, [directory](const cv::Mat& image, std::vector<uint8_t>& bytes) {
        bool okay = cv::imencode(".jpg", image, bytes, {cv::IMWRITE_JPEG_QUALITY, 80});
        int fd = openat(directory, "camera-source.json", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0) throw std::runtime_error("test manifest absent");
        std::array<char, 16385> data{};
        auto n = read(fd, data.data(), data.size());
        close(fd);
        google::protobuf::Value value;
        if (n <= 0 ||
            !google::protobuf::util::JsonStringToMessage(std::string(data.data(), n), &value).ok())
          throw std::runtime_error("test manifest parse");
        (*value.mutable_struct_value()->mutable_fields())["instance_id"].set_string_value(
            "changed-inside-codec");
        std::string json;
        google::protobuf::util::MessageToJsonString(value, &json);
        fd = openat(directory, ".race-manifest", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (fd < 0 || write(fd, json.data(), json.size()) != static_cast<ssize_t>(json.size()))
          throw std::runtime_error("test manifest write");
        close(fd);
        if (renameat(directory, ".race-manifest", directory, "camera-source.json"))
          throw std::runtime_error("test manifest replace");
        return okay;
      });
#endif
}
