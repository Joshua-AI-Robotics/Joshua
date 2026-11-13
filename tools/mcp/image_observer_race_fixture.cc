// Test-only codec failures and concurrent candidate-mapping edits.
#include <fcntl.h>
#include <unistd.h>

#include <fstream>
#include <iterator>
#include <opencv2/imgcodecs.hpp>
#include <string>

#include "tools/mcp/image_observer_backend.h"
int main(int argc, char** argv) {
  if (argc != 7) return 2;
#if defined(JOSHUA_TEST_CODEC_FAILURE)
  return joshua::mcp::RunImageObserver(
      argc, argv, [](const cv::Mat&, std::vector<uint8_t>&) { return false; });
#elif defined(JOSHUA_TEST_CODEC_OVERSIZE)
  return joshua::mcp::RunImageObserver(argc, argv, [](const cv::Mat&, std::vector<uint8_t>& bytes) {
    bytes.assign(512 * 1024 + 1, 0);
    return true;
  });
#else
  const std::string path = "/proc/self/fd/" + std::string(argv[2]) + "/" + argv[4];
  return joshua::mcp::RunImageObserver(
      argc, argv, [path](const cv::Mat& image, std::vector<uint8_t>& bytes) {
        bool okay = cv::imencode(".jpg", image, bytes, {cv::IMWRITE_JPEG_QUALITY, 80});
        std::ifstream input(path);
        std::string text((std::istreambuf_iterator<char>(input)), {});
        const std::string original = "/_joshua_image_fixture/image";
        const auto position = text.find(original);
        if (position == std::string::npos) throw std::runtime_error("fixture topic not found");
        text.replace(position, original.size(), "/changed_inside_codec");
        std::ofstream(path + ".tmp") << text;
        if (rename((path + ".tmp").c_str(), path.c_str()))
          throw std::runtime_error("fixture candidate replace");
        return okay;
      });
#endif
}
