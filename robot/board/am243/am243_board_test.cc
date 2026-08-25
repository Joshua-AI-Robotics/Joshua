#include "robot/board/am243/am243_board.h"

#include <memory>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "robot/board/am243/am243_pdo_codec.h"
#include "robot/board/proto/board.pb.h"
#include "robot/comm/ethercat/fake_ethercat_transport.h"
#include "robot/comm/factory/comm_factory.h"
#include "robot/comm/proto/comm.pb.h"

namespace robot::board {
namespace {

using robot::comm::CommFactory;
using robot::comm::ethercat::FakeEthercatTransport;

robot::board::Board MakeAm243Board() {
  robot::board::Board board;
  board.set_name("am243_1");
  board.set_board_type(robot::board::BoardType::AM243);

  auto* comm = board.mutable_comm();
  comm->set_comm_type(robot::comm::CommType::ETHERCAT);
  auto* ethercat_config = comm->mutable_ethercat_config();
  ethercat_config->set_interface_name("fake-am243-iface0");
  ethercat_config->set_process_data_mode(
      robot::comm::EthercatProcessDataMode::ETHERCAT_PROCESS_DATA_MODE_SPLIT_LRD_LWR);

  auto* channel = board.add_channels();
  channel->set_index(0);
  channel->set_drive(robot::board::DriveInterface::PDO_JOINT);

  auto* am243_config = board.mutable_am243_config();
  am243_config->set_slave_index(2);
  am243_config->set_pdo_mapping(robot::board::Am243PdoMapping::AM243_PDO_MAPPING_TI_DEMO);
  am243_config->set_output_offset_bytes(4);
  am243_config->set_input_offset_bytes(12);
  am243_config->set_output_size_bytes(8);
  am243_config->set_input_size_bytes(8);
  return board;
}

class Am243BoardTest : public ::testing::Test {
 protected:
  void SetUp() override {
    transport_ = std::make_shared<FakeEthercatTransport>();
    CommFactory::SetEthercatTransportFactoryForTesting([this] { return transport_; });
  }

  void TearDown() override {
    CommFactory::SetEthercatTransportFactoryForTesting(nullptr);
    CommFactory::ResetEthercatTransportCacheForTesting();
  }

  std::shared_ptr<FakeEthercatTransport> transport_;
};

TEST_F(Am243BoardTest, InitOwnsFullSoemLifecycle) {
  Am243Board board;

  auto status = board.Init(MakeAm243Board());

  EXPECT_TRUE(status.ok()) << status;
  EXPECT_EQ(transport_->init_calls_, 1);
  EXPECT_EQ(transport_->configure_slaves_calls_, 1);
  EXPECT_EQ(transport_->start_cyclic_calls_, 1);
  // The explicit region config wins over slave discovery.
  EXPECT_EQ(transport_->get_pdo_region_calls_, 0);
}

TEST_F(Am243BoardTest, InitRejectsWrongBoardType) {
  auto config = MakeAm243Board();
  config.set_board_type(robot::board::BoardType::MOCK);
  Am243Board board;

  EXPECT_EQ(board.Init(config).code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(Am243BoardTest, InitRejectsMissingEthercatComm) {
  auto config = MakeAm243Board();
  config.mutable_comm()->set_comm_type(robot::comm::CommType::SERIAL);
  Am243Board board;

  EXPECT_EQ(board.Init(config).code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(Am243BoardTest, InitRejectsLrwProcessDataMode) {
  auto config = MakeAm243Board();
  config.mutable_comm()->mutable_ethercat_config()->set_process_data_mode(
      robot::comm::EthercatProcessDataMode::ETHERCAT_PROCESS_DATA_MODE_LRW);
  Am243Board board;

  EXPECT_EQ(board.Init(config).code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(Am243BoardTest, InitRejectsUnspecifiedPdoMapping) {
  auto config = MakeAm243Board();
  config.mutable_am243_config()->set_pdo_mapping(
      robot::board::Am243PdoMapping::AM243_PDO_MAPPING_UNSPECIFIED);
  Am243Board board;

  EXPECT_EQ(board.Init(config).code(), absl::StatusCode::kUnimplemented);
}

TEST_F(Am243BoardTest, InitRejectsNonPdoJointChannel) {
  auto config = MakeAm243Board();
  config.mutable_channels(0)->set_drive(robot::board::DriveInterface::STEP_DIR);
  Am243Board board;

  EXPECT_EQ(board.Init(config).code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(Am243BoardTest, InitRejectsInvalidExplicitPdoRegion) {
  auto config = MakeAm243Board();
  config.mutable_am243_config()->set_input_size_bytes(0);
  Am243Board board;

  EXPECT_EQ(board.Init(config).code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(Am243BoardTest, InitFetchesPdoRegionWhenSizesAreZero) {
  auto config = MakeAm243Board();
  config.mutable_am243_config()->set_output_size_bytes(0);
  config.mutable_am243_config()->set_input_size_bytes(0);
  Am243Board board;

  auto status = board.Init(config);

  EXPECT_TRUE(status.ok()) << status;
  EXPECT_EQ(transport_->get_pdo_region_calls_, 1);
  EXPECT_EQ(transport_->requested_slave_index_, 2);
}

TEST_F(Am243BoardTest, InitSurfacesStartCyclicFailure) {
  transport_->start_cyclic_status_ =
      absl::Status(absl::StatusCode::kUnavailable, "slaves did not reach OPERATIONAL");
  Am243Board board;

  EXPECT_EQ(board.Init(MakeAm243Board()).code(), absl::StatusCode::kUnavailable);
}

TEST_F(Am243BoardTest, OpenChannelRejectsUndeclaredIndex) {
  Am243Board board;
  ASSERT_TRUE(board.Init(MakeAm243Board()).ok());

  EXPECT_EQ(board.OpenChannel(3).status().code(), absl::StatusCode::kNotFound);
}

TEST_F(Am243BoardTest, SetTargetStagesSeedAndExchanges) {
  transport_->process_data_.working_count = 3;
  transport_->process_data_.expected_working_count = 3;
  Am243Board board;
  ASSERT_TRUE(board.Init(MakeAm243Board()).ok());
  auto channel_or = board.OpenChannel(0);
  ASSERT_TRUE(channel_or.ok());

  auto status = (*channel_or)->SetTarget(TargetMode::kPosition, 127.5f);

  EXPECT_TRUE(status.ok()) << status;
  EXPECT_EQ(transport_->exchange_process_data_calls_, 1);
  EXPECT_EQ(transport_->last_write_region_.slave_index, 2);
  EXPECT_EQ(transport_->last_outputs_, am243::EncodeDemoOutputSeed(128));
}

TEST_F(Am243BoardTest, SetTargetClampsNativeValueToSeedRange) {
  Am243Board board;
  ASSERT_TRUE(board.Init(MakeAm243Board()).ok());
  auto channel_or = board.OpenChannel(0);
  ASSERT_TRUE(channel_or.ok());

  ASSERT_TRUE((*channel_or)->SetTarget(TargetMode::kPosition, 500.0f).ok());
  EXPECT_EQ(transport_->last_outputs_, am243::EncodeDemoOutputSeed(255));

  ASSERT_TRUE((*channel_or)->SetTarget(TargetMode::kPosition, -10.0f).ok());
  EXPECT_EQ(transport_->last_outputs_, am243::EncodeDemoOutputSeed(0));
}

TEST_F(Am243BoardTest, SetTargetReturnsWorkingCountMismatch) {
  transport_->process_data_.working_count = 2;
  transport_->process_data_.expected_working_count = 3;
  Am243Board board;
  ASSERT_TRUE(board.Init(MakeAm243Board()).ok());
  auto channel_or = board.OpenChannel(0);
  ASSERT_TRUE(channel_or.ok());

  EXPECT_EQ((*channel_or)->SetTarget(TargetMode::kPosition, 1.0f).code(),
            absl::StatusCode::kUnavailable);
}

TEST_F(Am243BoardTest, ReadFeedbackDecodesDemoEcho) {
  transport_->inputs_ = am243::EncodeDemoOutputSeed(42);
  Am243Board board;
  ASSERT_TRUE(board.Init(MakeAm243Board()).ok());
  auto channel_or = board.OpenChannel(0);
  ASSERT_TRUE(channel_or.ok());

  auto feedback_or = (*channel_or)->ReadFeedback();

  ASSERT_TRUE(feedback_or.ok()) << feedback_or.status();
  EXPECT_EQ(feedback_or->position, 42.0f);
}

TEST_F(Am243BoardTest, TeardownStopsCyclicExchange) {
  Am243Board board;
  ASSERT_TRUE(board.Init(MakeAm243Board()).ok());

  EXPECT_TRUE(board.Teardown().ok());
  EXPECT_EQ(transport_->stop_cyclic_calls_, 1);
}

}  // namespace
}  // namespace robot::board
