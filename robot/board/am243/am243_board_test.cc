// AM243 factory coverage: shared JoshuaWire engine and rejection of retired
// TI-demo configurations. No separate AM243 board implementation is needed.
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "firmware/common/joshua_wire_v1.h"
#include "gtest/gtest.h"
#include "robot/board/factory/board_factory.h"
#include "robot/board/proto/board.pb.h"
#include "robot/comm/factory/comm_factory.h"
#include "robot/comm/proto/comm.pb.h"
#include "robot/comm/testing/fake_legacy_message_transport.h"

namespace robot::board {
namespace {

std::vector<uint8_t> MakeIdentifyResponse(uint8_t n_channels,
                                          jw_board_id_t board_id = JW_BOARD_AM243) {
  jw_identify_response_t response{};
  response.board_id = board_id;
  response.n_channels = n_channels;
  for (uint8_t i = 0; i < n_channels; ++i) {
    response.channel_drives[i] = JW_DRIVE_STEP_DIR;
  }
  uint8_t buf[JW1_MAX_FRAME_LEN];
  const int len = jw1_encode_identify_response(buf, sizeof(buf), &response);
  return std::vector<uint8_t>(buf, buf + len);
}

std::vector<uint8_t> MakeStatusResponse(uint8_t cmd, uint8_t channel, jw_status_t status) {
  uint8_t buf[JW1_MAX_FRAME_LEN];
  const int len = jw1_encode_status_response(buf, sizeof(buf), cmd, channel, status);
  return std::vector<uint8_t>(buf, buf + len);
}

robot::board::Board MakeAm243Board() {
  robot::board::Board board;
  board.set_name("am243_stepper_bus");
  board.set_board_type(robot::board::BoardType::AM243);
  auto* comm = board.mutable_comm();
  comm->set_comm_type(robot::comm::CommType::SERIAL);
  comm->set_transport_type(robot::comm::TransportType::MESSAGE);
  comm->mutable_serial_config()->set_port("/dev/ttyACM0");
  comm->mutable_serial_config()->set_baudrate(115200);
  board.mutable_firmware()->set_min_proto_version(1);

  auto* channel = board.add_channels();
  channel->set_index(0);
  channel->set_drive(robot::board::DriveInterface::STEP_DIR);
  channel->mutable_step_dir()->set_max_pulse_rate_hz(20000);
  channel->mutable_step_dir()->set_enable_active_low(true);
  channel->mutable_step_dir()->set_step_pin(2);
  channel->mutable_step_dir()->set_dir_pin(3);
  channel->mutable_step_dir()->set_enable_pin(4);
  return board;
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
    serial_transport_ = std::make_shared<robot::comm::FakeLegacyMessageTransport>();
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

  std::shared_ptr<robot::comm::FakeLegacyMessageTransport> serial_transport_;
};

TEST_F(Am243BoardTest, InitSucceedsAgainstAm243Identity) {
  serial_transport_->QueueResponse(MakeIdentifyResponse(1));
  serial_transport_->QueueResponse(MakeStatusResponse(JW_CMD_CONFIGURE_CHANNEL, 0, JW_STATUS_OK));
  EXPECT_TRUE(BoardFactory::GetOrCreate(MakeAm243Board()).ok());
}

TEST_F(Am243BoardTest, RetiredTiDemoIsRejectedBeforeOpeningTransport) {
  auto result = BoardFactory::GetOrCreate(MakeAm243EthercatBoard());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(result.status().message().find("retired"), std::string::npos);
}

TEST_F(Am243BoardTest, InitRejectsNonAm243WireIdentity) {
  serial_transport_->QueueResponse(MakeIdentifyResponse(1, JW_BOARD_TEENSY41));
  EXPECT_EQ(BoardFactory::GetOrCreate(MakeAm243Board()).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

}  // namespace
}  // namespace robot::board
