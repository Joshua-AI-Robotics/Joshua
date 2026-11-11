#include <stdexcept>
#include <string>
#include <utility>

#include "config/proto/robot_clock.pb.h"
#include "pybind11/pybind11.h"
#include "utils/robot_time.h"

namespace py = pybind11;

PYBIND11_MODULE(_robot_time, module) {
  module.doc() = "Python access to Joshua's C++ process-global robot clock.";
  py::register_exception<joshua::RobotTimeError>(module, "RobotTimeError", PyExc_RuntimeError);
  module.def("RobotTime", &joshua::RobotTime);
  module.def("initialize", [](py::bytes serialized_config) {
    config::RobotClockConfig config;
    if (!config.ParseFromString(static_cast<std::string>(serialized_config))) {
      throw py::value_error("Invalid serialized RobotClockConfig");
    }
    auto clock = joshua::MakeRobotClock(config);
    if (!clock.ok()) throw std::runtime_error(clock.status().ToString());
    const auto status = joshua::SetGlobalRobotClock(std::move(*clock));
    if (!status.ok()) throw std::runtime_error(status.ToString());
  });
}
