#include "robot/perception/lidar/lds01_driver.h"
#include <glog/logging.h>

namespace robot::perception{

Lds01Driver::Lds01Driver(const std::shared_ptr<robot::comm::Serial>& serial, const robot::perception::Lidar& lidar_config)
    : serial_(serial){
    id_ = std::to_string(lidar_config.id());
}

absl::Status Lds01Driver::Init() {
    if(!serial_->Open().ok()) {
        LOG(ERROR) << "Failed to open serial port.";
        return absl::Status(absl::StatusCode::kInternal, "Failed to open serial port.");
    }
    stop_receiving_.store(false);
    return absl::OkStatus();
}

absl::Status Lds01Driver::Teardown() {
    stop_receiving_.store(true);
    return absl::OkStatus();
}

std::string Lds01Driver::GetId() {
    auto id = "lds01_driver_" + id_;
    return id;
}


absl::StatusOr<robot::perception::PerceptionPacket> Lds01Driver::GetData() {
    uint8_t start_count = 0;
    bool got_scan = false;
    std::vector<uint8_t> raw_bytes(2520);
    uint8_t good_sets = 0;
    uint32_t motor_speed = 0;
    uint32_t rpms = 0;
    uint16_t index;
    // Main loop for thread.
    while(stop_receiving_){
        while (!got_scan) {
            // Wait until first data sync of frame: 0xFA, 0xA0
            auto result = serial_->Read(1);

            if (!result.ok()) {
                LOG(ERROR) << "Failed to read from serial port.";
                continue;
            }
            raw_bytes = result.value();
            if (start_count == 0) {
              if (raw_bytes[start_count] == 0xFA) {
                start_count = 1;
              }
            } else if (start_count == 1) {
              if (raw_bytes[start_count] == 0xA0) {
                start_count = 0;
        
                // Start sequence found, read in the rest of the message
                got_scan = true;
        
                auto result = serial_->Read(2518);
                if (!result.ok()) {
                    LOG(ERROR) << "Failed to read from serial port.";
                    continue;
                }
                raw_bytes = result.value();
                for (uint16_t i = 0; i < raw_bytes.size(); i = i + 42) {
                  if (raw_bytes[i] == 0xFA && raw_bytes[i + 1] == (0xA0 + i / 42)) {
                    good_sets++;
                    motor_speed += (raw_bytes[i + 3] << 8) + raw_bytes[i + 2];
                    rpms = (raw_bytes[i + 3] << 8 | raw_bytes[i + 2]) / 10;
        
                    reusable_packet_.Clear();
                    for (uint16_t j = i + 4; j < i + 40; j = j + 6) {
                      auto polar = reusable_packet_.mutable_polar_coordinate()->add_polar_coordinates();

                      index = 6 * (i / 42) + (j - 4 - i) / 6;
        
                      uint8_t byte0 = raw_bytes[j];
                      uint8_t byte1 = raw_bytes[j + 1];
                      uint8_t byte2 = raw_bytes[j + 2];
                      uint8_t byte3 = raw_bytes[j + 3];
        
                      uint16_t intensity = (byte1 << 8) + byte0;
        
                      uint16_t range = (byte3 << 8) + byte2;
                        
                      polar->set_angle(index);
                      polar->set_distance(range);
                      polar->set_intensity(intensity);
                      
                      LOG(INFO) << "r[" << 359 - index << "]=" << range / 1000.0;
                    }
                  }
                }
        
              } else {
                start_count = 0;
              }
            }
          }
    }
  
  return reusable_packet_;
}
}