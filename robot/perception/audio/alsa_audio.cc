#include "robot/perception/audio/alsa_audio.h"

#include <algorithm>
#include <cerrno>
#include <chrono>

#include "robot/perception/audio/audio_config.h"
namespace robot::perception {
namespace {
absl::Status AlsaError(const char* operation, int error) {
  return absl::UnavailableError(std::string(operation) + ": " + snd_strerror(error));
}
uint64_t RealtimeNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
}  // namespace
AlsaAudio::AlsaAudio(const SinglePerception& config)
    : config_(config.alsa_audio_config()), id_(config.sensor_name()) {}
AlsaAudio::~AlsaAudio() {
  Teardown().IgnoreError();
}
absl::Status AlsaAudio::Init() {
  std::lock_guard<std::mutex> lock(mutex_);
  return OpenLocked();
}
absl::Status AlsaAudio::OpenLocked() {
  if (pcm_) return absl::OkStatus();
  auto valid = ValidateAudioConfig(config_);
  if (!valid.ok()) return valid;
  int error =
      snd_pcm_open(&pcm_, config_.device().c_str(), SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
  if (error < 0) {
    pcm_ = nullptr;
    return AlsaError("open audio", error);
  }
  // ALSA's plug device may resample; the published format is exactly this request.
  error = snd_pcm_set_params(pcm_,
                             SND_PCM_FORMAT_S16_LE,
                             SND_PCM_ACCESS_RW_INTERLEAVED,
                             config_.channels(),
                             config_.sample_rate_hz(),
                             1,
                             200000);
  if (error < 0) {
    CloseLocked();
    return AlsaError("configure audio", error);
  }
  error = snd_pcm_start(pcm_);
  if (error < 0) {
    CloseLocked();
    return AlsaError("start audio", error);
  }
  pending_.resize(static_cast<size_t>(config_.frames_per_chunk()) * config_.channels() * 2);
  filled_ = 0;
  discontinuity_ = true;
  return absl::OkStatus();
}
void AlsaAudio::CloseLocked() {
  if (pcm_) {
    snd_pcm_close(pcm_);
    pcm_ = nullptr;
  }
  filled_ = 0;
  discontinuity_ = true;
}
absl::Status AlsaAudio::Teardown() {
  std::lock_guard<std::mutex> lock(mutex_);
  CloseLocked();
  return absl::OkStatus();
}
absl::StatusOr<PerceptionPacket> AlsaAudio::GetData() {
  std::lock_guard<std::mutex> lock(mutex_);
  auto opened = OpenLocked();
  if (!opened.ok()) return opened;
  if (filled_ == 0) {
    // Available capture frames estimate the oldest unread sample's age.
    const auto available = snd_pcm_avail_update(pcm_);
    const uint64_t now = RealtimeNs();
    const uint64_t age =
        available > 0 ? static_cast<uint64_t>(available) * 1000000000ULL / config_.sample_rate_hz()
                      : 0;
    timestamp_ns_ = now - std::min(now, age);
  }
  const auto count = snd_pcm_readi(pcm_,
                                   pending_.data() + filled_ * config_.channels() * 2,
                                   config_.frames_per_chunk() - filled_);
  if (count == -EAGAIN || count == 0) return absl::UnavailableError("Audio chunk pending");
  if (count < 0) {
    // Discard partial data. Reopen on the next call; never splice across a gap.
    CloseLocked();
    return AlsaError("read audio (will reopen)", static_cast<int>(count));
  }
  filled_ += static_cast<uint32_t>(count);
  if (filled_ != config_.frames_per_chunk()) return absl::UnavailableError("Audio chunk pending");
  PerceptionPacket packet;
  packet.set_perception_id(id_);
  packet.set_timestamp_ns(timestamp_ns_);
  packet.set_sequence_id(sequence_++);
  auto* audio = packet.mutable_audio();
  audio->set_sample_rate_hz(config_.sample_rate_hz());
  audio->set_channels(config_.channels());
  audio->set_frames(filled_);
  audio->set_encoding("s16le");
  audio->set_data(pending_);
  audio->set_discontinuity(discontinuity_);
  filled_ = 0;
  discontinuity_ = false;
  return packet;
}
}  // namespace robot::perception
