// Pins version-neutral command values independently of either frame codec.
// Including only the semantic header verifies it has no v1/v2 dependency.
#include "firmware/common/joshua_wire_commands.h"

#include "gtest/gtest.h"

TEST(JoshuaWireCommands, WireIdsStayStable) {
  EXPECT_EQ(JW_CMD_IDENTIFY, 0x01);
  EXPECT_EQ(JW_CMD_CONFIGURE_CHANNEL, 0x02);
  EXPECT_EQ(JW_CMD_SET_TARGET, 0x03);
  EXPECT_EQ(JW_CMD_GET_FEEDBACK, 0x04);
  EXPECT_EQ(JW_CMD_ENABLE, 0x05);
  EXPECT_EQ(JW_CMD_DISABLE, 0x06);
  EXPECT_EQ(JW_CMD_ESTOP, 0x07);
  EXPECT_EQ(JW_CMD_RESET_SESSION, 0x08);
  EXPECT_EQ(JW_CHANNEL_NONE, 0xff);
  EXPECT_EQ(JW_MODE_POSITION, 0);
  EXPECT_EQ(JW_MODE_VELOCITY, 1);
  EXPECT_EQ(JW_MODE_TORQUE, 2);
  EXPECT_EQ(JW_STATUS_OK, 0);
  EXPECT_EQ(JW_STATUS_ERROR, 1);
  EXPECT_EQ(JW_STATUS_UNSUPPORTED, 2);
  EXPECT_EQ(JW_BOARD_INVALID, 0);
  EXPECT_EQ(JW_BOARD_AM243, 1);
  EXPECT_EQ(JW_BOARD_TEENSY41, 2);
  EXPECT_EQ(JW_BOARD_ARDUINO_UNO, 3);
  EXPECT_EQ(JW_BOARD_ESP32, 8);  // Historical wire ID, not protobuf ESP32=7.
  EXPECT_EQ(JW_DRIVE_INVALID, 0);
  EXPECT_EQ(JW_DRIVE_STEP_DIR, 1);
  EXPECT_EQ(JW_DRIVE_PWM_DC, 2);
  EXPECT_EQ(JW_DRIVE_SERVO_BUS_UART, 3);
  EXPECT_EQ(JW_DRIVE_CAN, 4);
  EXPECT_EQ(JW_DRIVE_PDO_JOINT, 5);
}

TEST(JoshuaWireCommands, PayloadLimitsStayStable) {
  EXPECT_EQ(JW_MAX_CHANNELS, 8);
  EXPECT_EQ(JW_FW_NAME_LEN, 16);
  EXPECT_EQ(JW_IDENTIFY_RESPONSE_PAYLOAD_LEN, 26);
  EXPECT_EQ(JW_FEEDBACK_RESPONSE_PAYLOAD_LEN, 10);
  EXPECT_EQ(JW_STATUS_RESPONSE_PAYLOAD_LEN, 1);
  jw_identify_response_t identity{};
  EXPECT_EQ(sizeof(identity.fw_name), JW_FW_NAME_LEN);
  EXPECT_EQ(sizeof(identity.channel_drives) / sizeof(identity.channel_drives[0]), JW_MAX_CHANNELS);
}
