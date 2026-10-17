#pragma once
#include <alsa/asoundlib.h>

#include <mutex>
#include <string>

#include "robot/perception/interfaces/audio_interface.h"
#include "robot/perception/proto/perception.pb.h"
namespace robot::perception {
// Nonblocking reads retain partial chunks. DeadlineExceeded means no complete
// chunk yet; Unavailable means a device failure that requires reopening.
class AlsaAudio : public AudioInterface {
 public:
  explicit AlsaAudio(const SinglePerception& config);
  ~AlsaAudio() override;
  absl::Status Init() override;
  std::string GetId() override {
    return id_;
  }
  absl::StatusOr<PerceptionPacket> GetData() override;
  absl::Status Teardown() override;

 private:
  absl::Status OpenLocked();
  void CloseLocked();
  std::mutex mutex_;
  AlsaAudioConfig config_;
  std::string id_;
  snd_pcm_t* pcm_ = nullptr;
  std::string pending_;
  uint32_t filled_ = 0;
  uint64_t timestamp_ns_ = 0;
  uint64_t sequence_ = 0;
  bool discontinuity_ = true;
};
}  // namespace robot::perception
