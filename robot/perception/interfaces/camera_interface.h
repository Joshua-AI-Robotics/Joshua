#pragma once

#include <opencv2/core/mat.hpp>
#include <memory>

namespace robot {
namespace perception {

class CameraInterface {
public:
    virtual ~CameraInterface() = default;

    virtual cv::Mat GetFrame() = 0;
};

}  // namespace perception
}  // namespace robot 