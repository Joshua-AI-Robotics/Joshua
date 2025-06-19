#include "robot/onboard/drivers/xbox_controller/xbox_controller.h"

#include <dirent.h>
#include <fcntl.h>
#include <iostream>
#include <linux/input.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <iomanip>

namespace robot {
namespace onboard {
namespace drivers {

XboxController::XboxController() {
    // No initial positions or servo maps in this class anymore
}

XboxController::~XboxController() {
    if (dev_) {
        libevdev_free(dev_);
    }
    if (fd_ != -1) {
        close(fd_);
    }
}

bool XboxController::Init() {
    LOG(INFO) << "Attempting to find Xbox controller...";
    if (!FindController()) {
        LOG(ERROR) << "Failed to find Xbox controller.";
        return false;
    }
    return true;
}

bool XboxController::FindController() {
    DIR *dir;
    struct dirent *ent;
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
                if (name && (strstr(name, "xbox") || strstr(name, "Xbox") || strstr(name, "XBox") || strstr(name, "MICROSOFT") || strstr(name, "Microsoft"))) {
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
    LOG(INFO) << "╔══════════════════════════════════════════════╗";
    LOG(INFO) << "║          Xbox Controller Raw Input Status    ║";
    LOG(INFO) << "╚══════════════════════════════════════════════╝\n";

    LOG(INFO) << "┌─────────────────────────┬──────────────────┐";
    LOG(INFO) << "│ Axis/Button             │ Value            │";
    LOG(INFO) << "├─────────────────────────┼──────────────────┤";
    LOG(INFO) << "│ D-Pad X                 │ " << std::setw(16) << std::right << state.abs_hat0x_value << " │";
    LOG(INFO) << "│ D-Pad Y                 │ " << std::setw(16) << std::right << state.abs_hat0y_value << " │";
    LOG(INFO) << "│ Left Joystick X         │ " << std::setw(16) << std::right << state.abs_x_value << " │";
    LOG(INFO) << "│ Left Joystick Y         │ " << std::setw(16) << std::right << state.abs_y_value << " │";
    LOG(INFO) << "│ Right Joystick X        │ " << std::setw(16) << std::right << state.abs_rx_value << " │";
    LOG(INFO) << "│ Right Joystick Y        │ " << std::setw(16) << std::right << state.abs_ry_value << " │";
    LOG(INFO) << "│ Left Trigger            │ " << std::setw(16) << std::right << state.abs_z_value << " │";
    LOG(INFO) << "│ Right Trigger           │ " << std::setw(16) << std::right << state.abs_rz_value << " │";
    LOG(INFO) << "│ A Button                │ " << std::setw(16) << std::right << state.btn_south_state << " │";
    LOG(INFO) << "│ B Button                │ " << std::setw(16) << std::right << state.btn_east_state << " │";
    LOG(INFO) << "│ X Button                │ " << std::setw(16) << std::right << state.btn_west_state << " │";
    LOG(INFO) << "│ Y Button                │ " << std::setw(16) << std::right << state.btn_north_state << " │";
    LOG(INFO) << "│ Left Bumper             │ " << std::setw(16) << std::right << state.btn_tl_state << " │";
    LOG(INFO) << "│ Right Bumper            │ " << std::setw(16) << std::right << state.btn_tr_state << " │";
    LOG(INFO) << "│ Start Button            │ " << std::setw(16) << std::right << state.btn_start_state << " │";
    LOG(INFO) << "│ Back Button             │ " << std::setw(16) << std::right << state.btn_select_state << " │";
    LOG(INFO) << "│ Left Stick Click        │ " << std::setw(16) << std::right << state.btn_thumbl_state << " │";
    LOG(INFO) << "│ Right Stick Click       │ " << std::setw(16) << std::right << state.btn_thumbr_state << " │";
    LOG(INFO) << "│ Guide Button            │ " << std::setw(16) << std::right << state.btn_mode_state << " │";
    LOG(INFO) << "└─────────────────────────┴──────────────────┘\n";

    LOG(INFO) << "  [Q] Quit";
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
        tv.tv_usec = 10000; // 10ms timeout

        int retval = select(fd_ + 1, &rfds, NULL, NULL, &tv);
        if (retval == -1) {
            LOG(ERROR) << "Error in select(): " << strerror(errno);
            break;
        } else if (retval) { // Data is available to read
            int rc = libevdev_next_event(dev_, LIBEVDEV_READ_FLAG_NORMAL, &ev);
            if (rc == LIBEVDEV_READ_STATUS_SYNC || rc == LIBEVDEV_READ_STATUS_SUCCESS) {
                ProcessEvent(ev, state);
            } else {
                LOG(ERROR) << "Error reading event: " << strerror(-rc);
                break;
            }
        } else { // retval == 0, timeout
            // No events within the timeout, continue loop
        }
        // PrintStatus(state);
    }

    // No torque disabling here, this class only handles controller input.
}

} // namespace drivers
} // namespace onboard
} // namespace robot 