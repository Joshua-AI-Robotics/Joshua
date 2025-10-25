#include "utils/xbox_controller/xbox_controller.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include <algorithm>
#include <iomanip>
#include <iostream>

namespace utils {

XboxController::XboxController() {
  // No initial positions or servo maps in this class anymore
}

XboxController::~XboxController() {
  Cleanup();
}

void XboxController::Cleanup() {
  if (dev_) {
    libevdev_free(dev_);
    dev_ = nullptr;
  }
  if (fd_ != -1) {
    close(fd_);
    fd_ = -1;
  }
}

bool XboxController::Init() {
  // Clean up any existing connection first
  Cleanup();

  LOG(INFO) << "Attempting to find Xbox controller...";
  if (!FindController()) {
    LOG(ERROR) << "Failed to find Xbox controller.";
    return false;
  }
  return true;
}

bool XboxController::FindController() {
  DIR* dir;
  struct dirent* ent;
  if ((dir = opendir("/dev/input")) != NULL) {
    while ((ent = readdir(dir)) != NULL) {
      if (strncmp(ent->d_name, "event", 5) == 0) {
        std::string path = "/dev/input/";
        path += ent->d_name;

        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
          LOG(WARNING) << "Failed to open " << path << ": " << strerror(errno);
          continue;
        }

        libevdev* dev = nullptr;
        int rc = libevdev_new_from_fd(fd, &dev);
        if (rc < 0) {
          LOG(WARNING) << "Failed to init libevdev for " << path << ": " << strerror(-rc);
          close(fd);
          continue;
        }

        const char* name = libevdev_get_name(dev);
        if (name && (strstr(name, "xbox") || strstr(name, "Xbox") || strstr(name, "XBox") ||
                     strstr(name, "MICROSOFT") || strstr(name, "Microsoft"))) {
          LOG(INFO) << "Found controller: " << name << " at " << path;
          dev_ = dev;
          fd_ = fd;
          closedir(dir);
          return true;
        } else {
          libevdev_free(dev);
          close(fd);
        }
      }
    }
    closedir(dir);
  }
  LOG(WARNING) << "No Xbox controller found.";
  return false;
}

void XboxController::PrintStatus(const XboxControllerState& state) {
  system("clear");
  LOG(INFO) << "\t╔══════════════════════════════════════════════╗";
  LOG(INFO) << "\t║       Xbox Controller Raw Input Status       ║";
  LOG(INFO) << "\t╚══════════════════════════════════════════════╝\n";

  LOG(INFO) << "\t┌─────────────────────────┬──────────────────┐";
  LOG(INFO) << "\t│ Axis/Button             │ Value            │";
  LOG(INFO) << "\t├─────────────────────────┼──────────────────┤";
  LOG(INFO) << "\t│ D-Pad X                 │ " << std::setw(16) << std::right
            << state.abs_hat0x_value << " │";
  LOG(INFO) << "\t│ D-Pad Y                 │ " << std::setw(16) << std::right
            << state.abs_hat0y_value << " │";
  LOG(INFO) << "\t│ Left Joystick X         │ " << std::setw(16) << std::right << state.abs_x_value
            << " │";
  LOG(INFO) << "\t│ Left Joystick Y         │ " << std::setw(16) << std::right << state.abs_y_value
            << " │";
  LOG(INFO) << "\t│ Right Joystick X        │ " << std::setw(16) << std::right << state.abs_rx_value
            << " │";
  LOG(INFO) << "\t│ Right Joystick Y        │ " << std::setw(16) << std::right << state.abs_ry_value
            << " │";
  LOG(INFO) << "\t│ Left Trigger            │ " << std::setw(16) << std::right << state.abs_z_value
            << " │";
  LOG(INFO) << "\t│ Right Trigger           │ " << std::setw(16) << std::right << state.abs_rz_value
            << " │";
  LOG(INFO) << "\t│ A Button                │ " << std::setw(16) << std::right
            << state.btn_south_state << " │";
  LOG(INFO) << "\t│ B Button                │ " << std::setw(16) << std::right
            << state.btn_east_state << " │";
  LOG(INFO) << "\t│ X Button                │ " << std::setw(16) << std::right
            << state.btn_west_state << " │";
  LOG(INFO) << "\t│ Y Button                │ " << std::setw(16) << std::right
            << state.btn_north_state << " │";
  LOG(INFO) << "\t│ Left Bumper             │ " << std::setw(16) << std::right << state.btn_tl_state
            << " │";
  LOG(INFO) << "\t│ Right Bumper            │ " << std::setw(16) << std::right << state.btn_tr_state
            << " │";
  LOG(INFO) << "\t│ Start Button            │ " << std::setw(16) << std::right
            << state.btn_start_state << " │";
  LOG(INFO) << "\t│ Back Button             │ " << std::setw(16) << std::right
            << state.btn_select_state << " │";
  LOG(INFO) << "\t│ Left Stick Click        │ " << std::setw(16) << std::right
            << state.btn_thumbl_state << " │";
  LOG(INFO) << "\t│ Right Stick Click       │ " << std::setw(16) << std::right
            << state.btn_thumbr_state << " │";
  LOG(INFO) << "\t│ Guide Button            │ " << std::setw(16) << std::right
            << state.btn_mode_state << " │";
  LOG(INFO) << "\t└─────────────────────────┴──────────────────┘\n";
}

void XboxController::ProcessEvent(const input_event& ev, XboxControllerState& state) {
  if (ev.type == EV_ABS) {
    auto it = state.abs_map_.find(ev.code);
    if (it != state.abs_map_.end()) {
      *(it->second) = ev.value;
    }
  } else if (ev.type == EV_KEY) {
    auto it = state.key_map_.find(ev.code);
    if (it != state.key_map_.end()) {
      *(it->second) = ev.value;
    }
  }
}

bool XboxController::ProcessEvents(XboxControllerState& state) {
  struct input_event ev;
  bool events_processed = false;

  // Process ALL available events in a single call to avoid lag
  while (true) {
    int rc = libevdev_next_event(dev_, LIBEVDEV_READ_FLAG_NORMAL, &ev);

    if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
      ProcessEvent(ev, state);
      events_processed = true;
    } else if (rc == LIBEVDEV_READ_STATUS_SYNC) {
      // Handle sync events (device state resync)
      ProcessEvent(ev, state);
      events_processed = true;
    } else if (rc == -EAGAIN) {
      // No more events available, break out of loop
      break;
    } else {
      // Real error occurred
      LOG(WARNING) << "Error reading event: " << strerror(-rc);
      break;
    }
  }

  return events_processed;
}

void XboxController::Run(XboxControllerState& state) {
  struct input_event ev;

  while (true) {
    // Check for Start button press to exit the loop.
    // Note: This only works if an event is processed that updates btn_start_state.
    // For immediate response, the main loop would also need to check this state.
    if (state.btn_start_state == 1) {
      LOG(INFO) << "Start button pressed in controller. Exiting controller loop.";
      break;
    }

    // Read event with a timeout
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd_, &rfds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 10000;  // 10ms timeout

    int retval = select(fd_ + 1, &rfds, NULL, NULL, &tv);
    if (retval == -1) {
      LOG(ERROR) << "Error in select(): " << strerror(errno);
      break;
    } else if (retval) {  // Data is available to read
      int rc = libevdev_next_event(dev_, LIBEVDEV_READ_FLAG_NORMAL, &ev);
      if (rc == LIBEVDEV_READ_STATUS_SYNC || rc == LIBEVDEV_READ_STATUS_SUCCESS) {
        ProcessEvent(ev, state);
      } else {
        LOG(WARNING) << "Error reading event: " << strerror(-rc);
      }
    } else {  // retval == 0, timeout
              // No events within the timeout, continue loop
    }
    // PrintStatus(state);
  }

  // No torque disabling here, this class only handles controller input.
}

}  // namespace utils
