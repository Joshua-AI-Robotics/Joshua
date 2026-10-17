#include "robot/perception/audio/alsa_audio.h"

#include "gtest/gtest.h"
namespace robot::perception {
TEST(AlsaAudioTest, RejectsInvalidConfigWithoutDeviceAccess) {
  SinglePerception config;
  AlsaAudio audio(config);
  EXPECT_EQ(audio.Init().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(audio.Teardown().ok());
}
TEST(AlsaAudioTest, NullPluginProducesFramedPacketsAndRestartDiscontinuity) {
  // ALSA's built-in null plugin does not access a microphone or other hardware.
  SinglePerception config;
  config.set_sensor_name("test_audio");
  auto* params = config.mutable_alsa_audio_config();
  params->set_device("null");
  params->set_sample_rate_hz(16000);
  params->set_channels(2);
  params->set_frames_per_chunk(320);
  AlsaAudio audio(config);
  ASSERT_TRUE(audio.Init().ok());
  auto first = audio.GetData();
  ASSERT_TRUE(first.ok()) << first.status();
  EXPECT_EQ(first->audio().data().size(), 1280);
  EXPECT_EQ(first->audio().frames(), 320);
  EXPECT_EQ(first->audio().sample_rate_hz(), 16000);
  EXPECT_EQ(first->audio().channels(), 2);
  EXPECT_EQ(first->audio().encoding(), "s16le");
  EXPECT_EQ(first->perception_id(), "test_audio");
  EXPECT_GT(first->timestamp_ns(), 1000000000000000000ULL);
  EXPECT_TRUE(first->audio().discontinuity());
  PerceptionPacket decoded;
  ASSERT_TRUE(decoded.ParseFromString(first->SerializeAsString()));
  EXPECT_EQ(decoded.audio().data(), first->audio().data());
  auto second = audio.GetData();
  ASSERT_TRUE(second.ok());
  EXPECT_FALSE(second->audio().discontinuity());
  EXPECT_EQ(second->sequence_id(), first->sequence_id() + 1);
  ASSERT_TRUE(audio.Teardown().ok());
  auto restarted = audio.GetData();
  ASSERT_TRUE(restarted.ok());
  EXPECT_TRUE(restarted->audio().discontinuity());
  EXPECT_EQ(restarted->sequence_id(), second->sequence_id() + 1);
}
}  // namespace robot::perception
