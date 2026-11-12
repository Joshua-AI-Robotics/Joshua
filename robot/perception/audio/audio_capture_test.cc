#include "robot/perception/audio/audio_capture.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <vector>

#include "gtest/gtest.h"
#include "robot/perception/audio/alsa_audio.h"

namespace {
using namespace std::chrono_literals;

// A finite capture buffer driven by virtual time. All PCM entrypoints are
// wrapped, so this test cannot open a device and needs no wall-clock sleeps.
struct FakePcm {
  snd_pcm_uframes_t buffer_frames = 3200;
  snd_pcm_uframes_t period_frames = 800;
  snd_pcm_uframes_t available = 0;
  snd_pcm_uframes_t max_read = std::numeric_limits<snd_pcm_uframes_t>::max();
  unsigned rate = 0;
  unsigned channels = 0;
  unsigned requested_latency_us = 0;
  uint64_t fractional_frames = 0;
  uint64_t produced = 0;
  uint64_t consumed = 0;
  int overruns = 0;
  int opens = 0;
  int read_error = 0;
  int params_error = 0;
  bool running = false;
  bool overrun = false;
  bool always_ready = false;

  void Advance(std::chrono::nanoseconds duration) {
    if (!running) return;
    constexpr auto ticks_per_second = std::chrono::nanoseconds(1s).count();
    fractional_frames += duration.count() * rate;
    const auto frames = fractional_frames / ticks_per_second;
    fractional_frames %= ticks_per_second;
    produced += frames;
    available += frames;
    if (available > buffer_frames && !overrun) {
      overrun = true;
      ++overruns;
    }
  }
};
FakePcm* pcm_state = nullptr;
}  // namespace

extern "C" int __wrap_snd_pcm_open(snd_pcm_t** pcm, const char*, snd_pcm_stream_t, int) {
  *pcm = reinterpret_cast<snd_pcm_t*>(pcm_state);
  ++pcm_state->opens;
  return 0;
}
extern "C" int __wrap_snd_pcm_close(snd_pcm_t*) {
  pcm_state->running = false;
  return 0;
}
extern "C" int __wrap_snd_pcm_set_params(snd_pcm_t*,
                                         snd_pcm_format_t,
                                         snd_pcm_access_t,
                                         unsigned channels,
                                         unsigned rate,
                                         int,
                                         unsigned latency) {
  pcm_state->channels = channels;
  pcm_state->rate = rate;
  pcm_state->requested_latency_us = latency;
  return 0;
}
extern "C" int __wrap_snd_pcm_get_params(snd_pcm_t*,
                                         snd_pcm_uframes_t* buffer,
                                         snd_pcm_uframes_t* period) {
  *buffer = pcm_state->buffer_frames;
  *period = pcm_state->period_frames;
  return pcm_state->params_error;
}
extern "C" int __wrap_snd_pcm_start(snd_pcm_t*) {
  pcm_state->available = 0;
  pcm_state->fractional_frames = 0;
  pcm_state->overrun = false;
  pcm_state->running = true;
  return 0;
}
extern "C" snd_pcm_sframes_t __wrap_snd_pcm_avail_update(snd_pcm_t*) {
  return pcm_state->overrun ? -EPIPE : static_cast<snd_pcm_sframes_t>(pcm_state->available);
}
extern "C" snd_pcm_sframes_t __wrap_snd_pcm_readi(snd_pcm_t*,
                                                  void* data,
                                                  snd_pcm_uframes_t frames) {
  auto& pcm = *pcm_state;
  if (pcm.read_error) {
    const auto error = pcm.read_error;
    pcm.read_error = 0;
    return error;
  }
  if (pcm.overrun) return -EPIPE;
  const auto count = std::min({frames, pcm.max_read, pcm.always_ready ? frames : pcm.available});
  if (!count) return -EAGAIN;
  // Each session gets distinct PCM bytes to detect splicing across a reopen.
  std::memset(data, pcm.opens, count * pcm.channels * sizeof(int16_t));
  if (!pcm.always_ready) pcm.available -= count;
  pcm.consumed += count;
  return count;
}

namespace robot::perception {
namespace {
class AudioCaptureTest : public ::testing::Test {
 protected:
  void SetUp() override {
    pcm_state = &pcm_;
    config_.set_sensor_name("fake_microphone");
    auto* audio = config_.mutable_alsa_audio_config();
    audio->set_device("fake");
    audio->set_sample_rate_hz(16000);
    audio->set_channels(1);
    audio->set_frames_per_chunk(320);
  }
  void CaptureFor(AlsaAudio& audio, std::chrono::nanoseconds duration, int delay_intervals = 1) {
    auto elapsed = 0ns;
    while (elapsed < duration) {
      const auto interval = audio.GetCaptureSchedule().poll_interval * delay_intervals;
      ASSERT_GT(interval.count(), 0);
      const auto advance = std::min(interval, duration - elapsed);
      pcm_.Advance(advance);
      elapsed += advance;
      const auto status =
          DrainAudio(audio, [&](const auto& packet) { packets_.push_back(packet); });
      ASSERT_TRUE(status.ok() || absl::IsDeadlineExceeded(status)) << status;
      ASSERT_EQ(pcm_.overruns, 0);
    }
  }
  FakePcm pcm_;
  SinglePerception config_;
  std::vector<PerceptionPacket> packets_;
};

TEST_F(AudioCaptureTest, OneSecondChunksAccumulateBeforeBufferOverflows) {
  config_.mutable_alsa_audio_config()->set_frames_per_chunk(16000);
  AlsaAudio audio(config_);
  ASSERT_TRUE(audio.Init().ok());
  EXPECT_EQ(pcm_.requested_latency_us, 200000);
  CaptureFor(audio, 3s);
  ASSERT_EQ(packets_.size(), 3);
  EXPECT_EQ(pcm_.consumed, pcm_.produced);
  EXPECT_EQ(pcm_.opens, 1);
  for (const auto& packet : packets_) EXPECT_EQ(packet.audio().frames(), 16000);
}

TEST_F(AudioCaptureTest, OneFrameChunksSustainSampleRateWithDelayedCallbacks) {
  config_.mutable_alsa_audio_config()->set_frames_per_chunk(1);
  AlsaAudio audio(config_);
  ASSERT_TRUE(audio.Init().ok());
  // Two milliseconds between calls produces 32 packets, exceeding the old
  // eight-packet budget even without relying on timer precision.
  CaptureFor(audio, 2s, 2);
  EXPECT_EQ(packets_.size(), 32000);
  EXPECT_EQ(pcm_.consumed, pcm_.produced);
  EXPECT_EQ(pcm_.available, 0);
}

TEST_F(AudioCaptureTest, SmallChunksSustainMaximumSupportedRateAndChannels) {
  auto* config = config_.mutable_alsa_audio_config();
  config->set_sample_rate_hz(192000);
  config->set_channels(8);
  config->set_frames_per_chunk(1);
  pcm_.buffer_frames = 38400;
  pcm_.period_frames = 9600;
  AlsaAudio audio(config_);
  ASSERT_TRUE(audio.Init().ok());
  CaptureFor(audio, 100ms);
  ASSERT_EQ(packets_.size(), 19200);
  EXPECT_EQ(packets_.front().audio().data().size(), 8 * sizeof(int16_t));
  EXPECT_EQ(pcm_.consumed, pcm_.produced);
}

TEST_F(AudioCaptureTest, FullBufferDrainsWithExistingPartialPacketAndRemainder) {
  pcm_.buffer_frames = 1000;  // Not a multiple of the 320-frame chunk.
  pcm_.period_frames = 250;
  AlsaAudio audio(config_);
  ASSERT_TRUE(audio.Init().ok());
  pcm_.Advance(19937500ns);  // 319 frames, leaving one frame to finish the packet.
  EXPECT_TRUE(absl::IsDeadlineExceeded(audio.GetData().status()));
  pcm_.Advance(62500us);  // A full buffer of 1000 additional frames.
  auto status = DrainAudio(audio, [&](const auto& packet) { packets_.push_back(packet); });
  EXPECT_TRUE(status.ok() || absl::IsDeadlineExceeded(status)) << status;
  EXPECT_EQ(packets_.size(), 4);
  EXPECT_EQ(pcm_.consumed, 1319);
  EXPECT_EQ(pcm_.available, 0);
  pcm_.Advance(17562500ns);  // Complete the retained 39-frame partial packet.
  auto packet = audio.GetData();
  ASSERT_TRUE(packet.ok()) << packet.status();
  EXPECT_EQ(packet->sequence_id(), 4);
}

TEST_F(AudioCaptureTest, NegotiatedBufferOverridesRequestAndPreferredTimerFloor) {
  config_.mutable_alsa_audio_config()->set_capture_buffer_duration_ms(100);
  config_.mutable_alsa_audio_config()->set_frames_per_chunk(16000);
  pcm_.buffer_frames = 8;  // Half a millisecond at 16 kHz.
  pcm_.period_frames = 8;
  AlsaAudio audio(config_);
  ASSERT_TRUE(audio.Init().ok());
  EXPECT_EQ(pcm_.requested_latency_us, 100000);
  EXPECT_LE(audio.GetCaptureSchedule().poll_interval, 250us);
  CaptureFor(audio, 1s);
  ASSERT_EQ(packets_.size(), 1);
  EXPECT_EQ(packets_.front().audio().frames(), 16000);
}

TEST_F(AudioCaptureTest, ShortReadsDrainAvailableFramesBeforeReturningPending) {
  pcm_.max_read = 3;
  AlsaAudio audio(config_);
  ASSERT_TRUE(audio.Init().ok());
  CaptureFor(audio, 1s);
  EXPECT_EQ(packets_.size(), 50);
  EXPECT_EQ(pcm_.consumed, 16000);
  EXPECT_EQ(pcm_.available, 0);
}

TEST_F(AudioCaptureTest, ReopenDiscardsPartialPacketAndRefreshesNegotiatedSchedule) {
  config_.mutable_alsa_audio_config()->set_frames_per_chunk(16000);
  AlsaAudio audio(config_);
  ASSERT_TRUE(audio.Init().ok());
  CaptureFor(audio, 1100ms);
  ASSERT_EQ(packets_.size(), 1);
  const auto old_interval = audio.GetCaptureSchedule().poll_interval;
  pcm_.read_error = -EPIPE;
  EXPECT_TRUE(absl::IsUnavailable(audio.GetData().status()));
  // Recovery backoff occurs while the device is closed, so it cannot overflow.
  pcm_.Advance(1s);
  pcm_.buffer_frames = 160;
  pcm_.period_frames = 40;
  EXPECT_TRUE(absl::IsDeadlineExceeded(audio.GetData().status()));
  EXPECT_LT(audio.GetCaptureSchedule().poll_interval, old_interval);
  CaptureFor(audio, 1s);
  ASSERT_EQ(packets_.size(), 2);
  const auto& recovered = packets_.back();
  EXPECT_TRUE(recovered.audio().discontinuity());
  EXPECT_EQ(recovered.sequence_id(), packets_.front().sequence_id() + 1);
  EXPECT_EQ(recovered.audio().data(), std::string(16000 * sizeof(int16_t), '\2'));
  EXPECT_EQ(pcm_.opens, 2);
}

TEST_F(AudioCaptureTest, AlwaysReadySourceStillHasFinitePollBudget) {
  pcm_.always_ready = true;
  config_.mutable_alsa_audio_config()->set_frames_per_chunk(1);
  AlsaAudio audio(config_);
  ASSERT_TRUE(audio.Init().ok());
  const auto budget = audio.GetCaptureSchedule().max_packets_per_poll;
  uint64_t packets = 0;
  EXPECT_TRUE(DrainAudio(audio, [&](const auto&) { ++packets; }).ok());
  EXPECT_EQ(packets, budget);
  EXPECT_EQ(pcm_.consumed, budget);
}

TEST_F(AudioCaptureTest, RejectsFailedOrEmptyNegotiatedBufferBeforeStarting) {
  AlsaAudio audio(config_);
  pcm_.params_error = -EINVAL;
  EXPECT_TRUE(absl::IsUnavailable(audio.Init()));
  EXPECT_FALSE(pcm_.running);
  pcm_.params_error = 0;
  pcm_.buffer_frames = 0;
  EXPECT_TRUE(absl::IsUnavailable(audio.Init()));
  EXPECT_FALSE(pcm_.running);
  pcm_.buffer_frames = 3200;
  pcm_.period_frames = 0;
  EXPECT_TRUE(absl::IsUnavailable(audio.Init()));
  EXPECT_FALSE(pcm_.running);
}
}  // namespace
}  // namespace robot::perception
