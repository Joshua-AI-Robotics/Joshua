#pragma once
#include <glog/logging.h>
#include <libevdev/libevdev.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace utils {

// Struct to hold the current state of the Xbox controller
struct XboxControllerState {
  // Axis values (raw values from evdev)
  int abs_hat0x_value;
  int abs_hat0y_value;
  int abs_x_value;
  int abs_y_value;
  int abs_rx_value;
  int abs_ry_value;
  int abs_z_value;   // Left Trigger
  int abs_rz_value;  // Right Trigger

  // Button states (0 for released, 1 for pressed)
  int btn_south_state;  // A
  int btn_east_state;   // B
  int btn_west_state;   // X
  int btn_north_state;  // Y
  int btn_tl_state;     // Left Bumper
  int btn_tr_state;     // Right Bumper
  int btn_start_state;
  int btn_select_state;  // Back
  int btn_thumbl_state;
  int btn_thumbr_state;
  int btn_mode_state;  // Guide/Xbox button

  // Mapping of evdev codes to specific state variables
  std::map<int, int*> abs_map_;
  std::map<int, int*> key_map_;

  XboxControllerState() {
    abs_map_[ABS_HAT0X] = &abs_hat0x_value;
    abs_map_[ABS_HAT0Y] = &abs_hat0y_value;
    abs_map_[ABS_X] = &abs_x_value;
    abs_map_[ABS_Y] = &abs_y_value;
    abs_map_[ABS_RX] = &abs_rx_value;
    abs_map_[ABS_RY] = &abs_ry_value;
    abs_map_[ABS_Z] = &abs_z_value;
    abs_map_[ABS_RZ] = &abs_rz_value;

    key_map_[BTN_SOUTH] = &btn_south_state;
    key_map_[BTN_EAST] = &btn_east_state;
    key_map_[BTN_WEST] = &btn_west_state;
    key_map_[BTN_NORTH] = &btn_north_state;
    key_map_[BTN_TL] = &btn_tl_state;
    key_map_[BTN_TR] = &btn_tr_state;
    key_map_[BTN_START] = &btn_start_state;
    key_map_[BTN_SELECT] = &btn_select_state;
    key_map_[BTN_THUMBL] = &btn_thumbl_state;
    key_map_[BTN_THUMBR] = &btn_thumbr_state;
    key_map_[BTN_MODE] = &btn_mode_state;

    // Initialize all values to 0
    abs_hat0x_value = 0;
    abs_hat0y_value = 0;
    abs_x_value = 0;
    abs_y_value = 0;
    abs_rx_value = 0;
    abs_ry_value = 0;
    abs_z_value = 0;
    abs_rz_value = 0;
    btn_south_state = 0;
    btn_east_state = 0;
    btn_west_state = 0;
    btn_north_state = 0;
    btn_tl_state = 0;
    btn_tr_state = 0;
    btn_start_state = 0;
    btn_select_state = 0;
    btn_thumbl_state = 0;
    btn_thumbr_state = 0;
    btn_mode_state = 0;
  }
};

class XboxController {
 public:
  explicit XboxController();
  ~XboxController();

  bool Init();
  void Run(XboxControllerState& state);
  bool ProcessEvents(XboxControllerState& state);  // New method for non-blocking event processing
  void Cleanup();                                  // Add cleanup method

 private:
  static const int XBOX_JOYSTICK_MIN = -32768;
  static const int XBOX_JOYSTICK_MAX = 32767;
  static const int XBOX_TRIGGER_MIN = 0;
  static const int XBOX_TRIGGER_MAX = 1023;

  libevdev* dev_ = nullptr;
  int fd_ = -1;

  bool FindController();
  void ProcessEvent(const input_event& ev, XboxControllerState& state);
  void PrintStatus(const XboxControllerState& state);
};

}  // namespace utils
