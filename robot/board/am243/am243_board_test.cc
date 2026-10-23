// AM243 factory coverage: shared JoshuaWire engine and rejection of retired
// TI-demo configurations. No separate AM243 board implementation is needed.
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "robot/board/factory/board_factory.h"
#include "robot/board/joshua_wire/testing/board_config.h"
#include "robot/board/joshua_wire/testing/fake_joshua_wire_transport.h"
#include "robot/board/proto/board.pb.h"
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
          ADD_FAILURE() << "Retired EtherCAT config reached comm construction";
          return absl::InternalError("unexpected backend access");
        });
  }

  void TearDown() override {
    BoardFactory::ResetForTesting();
    robot::comm::CommFactory::SetCommTransportFactoryForTesting(nullptr);
    robot::comm::CommFactory::ResetEthercatTransportCacheForTesting();
  }

  std::shared_ptr<FakeJoshuaWireTransport> serial_transport_;
};

TEST_F(Am243BoardTest, InitSucceedsAgainstAm243Identity) {
  EXPECT_TRUE(BoardFactory::GetOrCreate(MakeAm243Board()).ok());
}

TEST_F(Am243BoardTest, RetiredTiDemoIsRejectedBeforeOpeningTransport) {
  auto result = BoardFactory::GetOrCreate(MakeAm243EthercatBoard());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(result.status().message().find("retired"), std::string::npos);
}

TEST_F(Am243BoardTest, InitRejectsRetiredWireRevision) {
  serial_transport_->channel.runtime.identity.protocol_min = 1;
  serial_transport_->channel.runtime.identity.protocol_max = 1;
  EXPECT_EQ(BoardFactory::GetOrCreate(MakeAm243Board()).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

}  // namespace
}  // namespace robot::board
