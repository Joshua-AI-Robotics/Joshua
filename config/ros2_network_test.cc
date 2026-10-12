#include "config/ros2_network.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>

#include "gtest/gtest.h"

namespace config {
namespace {
class NetworkTest : public testing::Test {
 protected:
  void SetUp() override {
    for (const auto* key : {"ROS_DOMAIN_ID",
                            "ROS_LOCALHOST_ONLY",
                            "RMW_IMPLEMENTATION",
                            "ROS_AUTOMATIC_DISCOVERY_RANGE",
                            "ROS_DISCOVERY_SERVER",
                            "ROS_STATIC_PEERS",
                            "ROS_SUPER_CLIENT",
                            "FASTRTPS_DEFAULT_PROFILES_FILE",
                            "FASTDDS_DEFAULT_PROFILES_FILE"}) {
      const char* value = std::getenv(key);
      previous_[key] = value ? std::optional<std::string>(value) : std::nullopt;
      unsetenv(key);
    }
  }
  void TearDown() override {
    for (const auto& [key, value] : previous_) {
      if (value)
        setenv(key.c_str(), value->c_str(), 1);
      else
        unsetenv(key.c_str());
    }
  }
  Ros2NetworkConfig Peers() {
    Ros2NetworkConfig config;
    config.set_discovery(Ros2NetworkConfig::STATIC_PEERS);
    config.add_peers("192.0.2.2");
    return config;
  }
  std::map<std::string, std::optional<std::string>> previous_;
};

TEST_F(NetworkTest, AbsentSettingsPreserveExternalDiscovery) {
  setenv("RMW_IMPLEMENTATION", "rmw_cyclonedds_cpp", 1);
  setenv("ROS_DISCOVERY_SERVER", "192.0.2.3:11811", 1);
  auto network = Ros2NetworkEnvironment::Prepare({});
  ASSERT_TRUE(network.ok()) << network.status();
  EXPECT_TRUE((*network)->variables().empty());
  EXPECT_TRUE((*network)->Apply().ok());
  EXPECT_STREQ(getenv("RMW_IMPLEMENTATION"), "rmw_cyclonedds_cpp");
}

TEST_F(NetworkTest, ExplicitZeroOverridesInheritedDomainAndRestoresIt) {
  setenv("ROS_DOMAIN_ID", "7", 1);
  Ros2NetworkConfig config;
  config.set_domain_id(0);
  {
    auto network = Ros2NetworkEnvironment::Prepare(config);
    ASSERT_TRUE(network.ok());
    ASSERT_TRUE((*network)->Apply().ok());
    EXPECT_STREQ(getenv("ROS_DOMAIN_ID"), "0");
  }
  EXPECT_STREQ(getenv("ROS_DOMAIN_ID"), "7");
}

TEST_F(NetworkTest, RejectsInvalidSettingsBeforeCreatingProfiles) {
  auto config = Peers();
  config.clear_peers();
  EXPECT_FALSE(ValidateRos2Network(config).ok());
  for (const auto* peer :
       {"", "host.local", "1.2.3.999", "0.0.0.0", "239.255.0.1", "255.255.255.255", "<xml>"}) {
    config.clear_peers();
    config.add_peers(peer);
    EXPECT_FALSE(ValidateRos2Network(config).ok()) << peer;
  }
  config = Peers();
  config.set_participant_scan_count(0);
  EXPECT_FALSE(ValidateRos2Network(config).ok());
  config.set_participant_scan_count(121);
  EXPECT_FALSE(ValidateRos2Network(config).ok());
  config.set_participant_scan_count(120);
  config.set_domain_id(102);
  EXPECT_FALSE(ValidateRos2Network(config).ok());
  config.set_domain_id(101);
  EXPECT_FALSE(ValidateRos2Network(config).ok());
  config.set_participant_scan_count(32);
  EXPECT_TRUE(ValidateRos2Network(config).ok());
  config.set_discovery(Ros2NetworkConfig::LOCALHOST);
  EXPECT_FALSE(ValidateRos2Network(config).ok());
  config.clear_peers();
  config.clear_participant_scan_count();
  EXPECT_TRUE(ValidateRos2Network(config).ok());
}

TEST_F(NetworkTest, InheritedDomainAffectsAllPeerPortsAndProfileLifetime) {
  setenv("ROS_DOMAIN_ID", "17", 1);
  auto config = Peers();
  config.set_participant_scan_count(3);
  std::string path;
  {
    auto network = Ros2NetworkEnvironment::Prepare(config);
    ASSERT_TRUE(network.ok()) << network.status();
    path = (*network)->variables().at("FASTRTPS_DEFAULT_PROFILES_FILE");
    std::ifstream file(path);
    std::string xml{std::istreambuf_iterator<char>(file), {}};
    EXPECT_NE(xml.find("127.0.0.1"), std::string::npos);
    EXPECT_NE(xml.find("192.0.2.2"), std::string::npos);
    EXPECT_NE(xml.find("<port>11660</port>"), std::string::npos);
    EXPECT_NE(xml.find("<port>11664</port>"), std::string::npos);
    EXPECT_EQ(xml.find("<port>11666</port>"), std::string::npos);
    ASSERT_TRUE((*network)->Apply().ok());
    {
      auto child = Ros2NetworkEnvironment::Prepare(config);
      ASSERT_TRUE(child.ok()) << child.status();
      EXPECT_EQ((*child)->variables().at("FASTRTPS_DEFAULT_PROFILES_FILE"), path);
      ASSERT_TRUE((*child)->Apply().ok());
    }
    EXPECT_TRUE(std::filesystem::exists(path));
  }
  EXPECT_FALSE(std::filesystem::exists(path));
  EXPECT_EQ(getenv("FASTRTPS_DEFAULT_PROFILES_FILE"), nullptr);
}

TEST_F(NetworkTest, RejectsConflictingExternalDiscovery) {
  for (const auto* key : {"RMW_IMPLEMENTATION",
                          "ROS_DISCOVERY_SERVER",
                          "ROS_SUPER_CLIENT",
                          "ROS_STATIC_PEERS",
                          "FASTRTPS_DEFAULT_PROFILES_FILE",
                          "FASTDDS_DEFAULT_PROFILES_FILE"}) {
    setenv(key, "conflict", 1);
    EXPECT_FALSE(Ros2NetworkEnvironment::Prepare(Peers()).ok()) << key;
    unsetenv(key);
  }
  for (const auto* domain : {"-1", "102", "abc", "0garbage"}) {
    setenv("ROS_DOMAIN_ID", domain, 1);
    EXPECT_FALSE(Ros2NetworkEnvironment::Prepare(Peers()).ok()) << domain;
  }
}

TEST_F(NetworkTest, ManagedModesOverrideLocalhostRestrictions) {
  for (auto mode : {Ros2NetworkConfig::AUTOMATIC, Ros2NetworkConfig::LOCALHOST}) {
    Ros2NetworkConfig config;
    config.set_discovery(mode);
    auto network = Ros2NetworkEnvironment::Prepare(config);
    ASSERT_TRUE(network.ok()) << network.status();
    EXPECT_EQ((*network)->variables().at("ROS_LOCALHOST_ONLY"),
              mode == Ros2NetworkConfig::LOCALHOST ? "1" : "0");
    EXPECT_EQ((*network)->variables().at("ROS_AUTOMATIC_DISCOVERY_RANGE"),
              mode == Ros2NetworkConfig::LOCALHOST ? "LOCALHOST" : "SUBNET");
  }
}
}  // namespace
}  // namespace config
