#include "robot/board/am243/am243_board.h"

#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "robot/board/joshua_wire/testing/board_config.h"
#include "robot/board/joshua_wire/testing/fake_joshua_wire_transport.h"
#include "robot/board/proto/board.pb.h"
#include "robot/comm/ethercat/fake_ethercat_transport.h"
#include "robot/comm/factory/comm_factory.h"
#include "robot/comm/proto/comm.pb.h"

namespace robot::board {
namespace {

robot::board::Board MakeAm243Board() {
  return MakeStepDirBoardConfig(AM243);
}

robot::board::Board MakeAm243EthercatBoard() {
  robot::board::Board board;
  board.set_name("am243_ethercat");
  board.set_board_type(robot::board::BoardType::AM243);
  auto* comm = board.mutable_comm();
  comm->set_comm_type(robot::comm::CommType::ETHERCAT);
  comm->set_transport_type(robot::comm::TransportType::CYCLIC);
  comm->mutable_ethercat_config()->set_interface_name("fake-am243-iface0");
  comm->mutable_ethercat_config()->set_process_data_mode(
      robot::comm::EthercatProcessDataMode::ETHERCAT_PROCESS_DATA_MODE_SPLIT_LRD_LWR);
  auto* channel = board.add_channels();
  channel->set_index(0);
  channel->set_drive(robot::board::DriveInterface::PDO_JOINT);
  auto* am243 = board.mutable_am243_config();
  am243->set_slave_index(2);
  am243->set_pdo_mapping(robot::board::Am243PdoMapping::AM243_PDO_MAPPING_TI_DEMO);
  am243->set_output_offset_bytes(4);
  am243->set_input_offset_bytes(12);
  am243->set_output_size_bytes(8);
  am243->set_input_size_bytes(8);
  return board;
}

class Am243BoardTest : public ::testing::Test {
 protected:
  void SetUp() override {
    serial_transport_ = std::make_shared<FakeJoshuaWireTransport>();
    robot::comm::CommFactory::SetCommTransportFactoryForTesting(
        [this](const robot::comm::Comm& comm) -> absl::StatusOr<robot::comm::CommTransport> {
          if (comm.comm_type() == robot::comm::CommType::SERIAL) {
            return robot::comm::CommTransport{
                std::static_pointer_cast<robot::comm::MessageTransport>(serial_transport_)};
          }
          return robot::comm::CommTransport{ethercat_transport_};
        });
    ethercat_transport_ = std::make_shared<robot::comm::ethercat::FakeEthercatTransport>();
  }

  void TearDown() override {
    robot::comm::CommFactory::SetCommTransportFactoryForTesting(nullptr);
    robot::comm::CommFactory::ResetEthercatTransportCacheForTesting();
  }

  std::shared_ptr<FakeJoshuaWireTransport> serial_transport_;
  std::shared_ptr<robot::comm::ethercat::FakeEthercatTransport> ethercat_transport_;
};

TEST_F(Am243BoardTest, InitSucceedsAgainstAm243Identity) {
  Am243Board board;

  EXPECT_TRUE(board.Init(MakeAm243Board()).ok());
}

TEST_F(Am243BoardTest, InitRejectsNonAm243BoardType) {
  auto config = MakeAm243Board();
  config.set_board_type(robot::board::BoardType::TEENSY41);
  Am243Board board;

  EXPECT_EQ(board.Init(config).code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(Am243BoardTest, InitSupportsEthercatDemoConfig) {
  Am243Board board;

  EXPECT_TRUE(board.Init(MakeAm243EthercatBoard()).ok());
  EXPECT_EQ(ethercat_transport_->configure_slaves_calls_, 1);
  EXPECT_EQ(ethercat_transport_->start_cyclic_calls_, 1);
}

TEST_F(Am243BoardTest, InitRejectsRetiredWireRevision) {
  serial_transport_->channel.runtime.identity.protocol_min = 1;
  serial_transport_->channel.runtime.identity.protocol_max = 1;
  Am243Board board;

  EXPECT_EQ(board.Init(MakeAm243Board()).code(), absl::StatusCode::kFailedPrecondition);
}

}  // namespace
}  // namespace robot::board
