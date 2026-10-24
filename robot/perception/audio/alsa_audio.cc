#include "robot/perception/audio/alsa_audio.h"

#include <algorithm>
#include <cerrno>
#include <chrono>

#include "robot/perception/audio/audio_config.h"
namespace robot::perception {
namespace {
constexpr size_t kBytesPerSample = sizeof(int16_t);  // Interleaved s16le PCM.
constexpr int kAllowAlsaResampling = 1;
// Batch very small packets to avoid one timer wakeup per sample. This floor is
// overridden when the negotiated hardware buffer requires faster polling.
constexpr auto kPreferredMinPollInterval = std::chrono::milliseconds(1);
// Aim for two polls per chunk and require two per buffer, reserving one poll
// interval of scheduling headroom even when tiny chunks are batched.
constexpr int kPollsPerWindow = 2;
std::chrono::nanoseconds FramesDuration(uint64_t frames, uint32_t rate) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<double>(static_cast<double>(frames) / rate));
}
absl::Status AlsaError(const char* operation, int error) {
  return absl::UnavailableError(std::string(operation) + ": " + snd_strerror(error));
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
AudioCaptureSchedule AlsaAudio::GetCaptureSchedule() {
  std::lock_guard<std::mutex> lock(mutex_);
  return schedule_;
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
  error = snd_pcm_set_params(
      pcm_,
      SND_PCM_FORMAT_S16_LE,
      SND_PCM_ACCESS_RW_INTERLEAVED,
      config_.channels(),
      config_.sample_rate_hz(),
      kAllowAlsaResampling,
      std::chrono::duration_cast<std::chrono::microseconds>(CaptureBufferDuration(config_))
          .count());
  if (error < 0) {
    CloseLocked();
    return AlsaError("configure audio", error);
  }
  snd_pcm_uframes_t buffer_frames = 0;
  snd_pcm_uframes_t period_frames = 0;
  error = snd_pcm_get_params(pcm_, &buffer_frames, &period_frames);
  if (error < 0) {
    CloseLocked();
    return AlsaError("query audio buffer", error);
  }
  if (buffer_frames == 0 || period_frames == 0) {
    CloseLocked();
    return absl::UnavailableError("ALSA negotiated an empty buffer or period");
  }
  const auto chunk_frames = config_.frames_per_chunk();
  const auto preferred_interval =
      std::max(std::chrono::duration_cast<std::chrono::nanoseconds>(kPreferredMinPollInterval),
               FramesDuration(chunk_frames, config_.sample_rate_hz()) / kPollsPerWindow);
  schedule_.poll_interval =
      std::min({preferred_interval,
                FramesDuration(period_frames, config_.sample_rate_hz()),
                FramesDuration(buffer_frames, config_.sample_rate_hz()) / kPollsPerWindow});
  // Round up to cover a whole buffer, plus one call for a preexisting partial
  // packet (or a final pending read). This scales with tiny chunk sizes too.
  schedule_.max_packets_per_poll =
      buffer_frames / chunk_frames + (buffer_frames % chunk_frames != 0) + 1;
  error = snd_pcm_start(pcm_);
  if (error < 0) {
    CloseLocked();
    return AlsaError("start audio", error);
  }
  pending_.resize(static_cast<size_t>(chunk_frames) * config_.channels() * kBytesPerSample);
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
    const double now = RobotTime();
    const double age =
        available > 0 ? static_cast<double>(available) / config_.sample_rate_hz() : 0.0;
    timestamp_ = now - age;
  }
  // A short successful read need not mean the device is empty. Continue until
  // a packet is complete or ALSA reports no data. Each successful read advances
  // filled_, so even one-frame reads are bounded by the packet's frame count.
  while (filled_ < config_.frames_per_chunk()) {
    const auto count =
        snd_pcm_readi(pcm_,
                      pending_.data() + filled_ * config_.channels() * kBytesPerSample,
                      config_.frames_per_chunk() - filled_);
    if (count == -EAGAIN || count == 0) return absl::DeadlineExceededError("Audio chunk pending");
    if (count < 0) {
      // Discard partial data. Reopen on the next call; never splice across a gap.
      CloseLocked();
      return AlsaError("read audio (will reopen)", static_cast<int>(count));
    }
    filled_ += static_cast<uint32_t>(count);
  }
  PerceptionPacket packet;
  packet.set_perception_id(id_);
  packet.set_timestamp(timestamp_);
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
