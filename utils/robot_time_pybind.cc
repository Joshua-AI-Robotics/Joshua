#include <stdexcept>
#include <string>

#include "pybind11/pybind11.h"
#include "utils/robot_time.h"

namespace py = pybind11;

PYBIND11_MODULE(_robot_time, module) {
  module.def("robot_time", &joshua::RobotTime, py::call_guard<py::gil_scoped_release>());
  module.def("robot_time_ns", &joshua::RobotTimeNs, py::call_guard<py::gil_scoped_release>());
  module.def("initialize", [](const py::bytes& serialized) {
    config::RobotClockConfig config;
    if (!config.ParseFromString(static_cast<std::string>(serialized)))
      throw py::value_error("Cannot parse RobotClockConfig");
    auto clock = joshua::MakeRobotClock(config);
    if (!clock.ok()) throw std::runtime_error(clock.status().ToString());
    const std::string source((*clock)->SourceName());
    auto status = joshua::SetGlobalRobotClock(std::move(*clock));
    if (!status.ok()) throw std::runtime_error(status.ToString());
    return source;
  });
}
