#include "robot/board/factory/board_factory.h"

#include "gtest/gtest.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {
namespace {

robot::board::Board MakeMockBoard(const std::string& name, int num_channels) {
  robot::board::Board board;
  board.set_name(name);
  board.set_board_type(robot::board::BoardType::MOCK);
  for (int i = 0; i < num_channels; ++i) {
    auto* channel = board.add_channels();
    channel->set_index(i);
    channel->set_drive(robot::board::DriveInterface::STEP_DIR);
  }
  return board;
}

class BoardFactoryTest : public ::testing::Test {
 protected:
  void TearDown() override {
    BoardFactory::ResetForTesting();
  }
};

TEST_F(BoardFactoryTest, SameNameSharesOneInstance) {
  auto board_a = BoardFactory::GetOrCreate(MakeMockBoard("bridge_1", 2));
  auto board_b = BoardFactory::GetOrCreate(MakeMockBoard("bridge_1", 2));
  ASSERT_TRUE(board_a.ok()) << board_a.status();
  ASSERT_TRUE(board_b.ok()) << board_b.status();
  EXPECT_EQ(board_a->get(), board_b->get());
}

TEST_F(BoardFactoryTest, DifferentNamesGetDifferentInstances) {
  auto board_a = BoardFactory::GetOrCreate(MakeMockBoard("bridge_1", 1));
  auto board_b = BoardFactory::GetOrCreate(MakeMockBoard("bridge_2", 1));
  ASSERT_TRUE(board_a.ok()) << board_a.status();
  ASSERT_TRUE(board_b.ok()) << board_b.status();
  EXPECT_NE(board_a->get(), board_b->get());
}

TEST_F(BoardFactoryTest, MissingNameIsRejected) {
  robot::board::Board board = MakeMockBoard("", 1);
  auto result = BoardFactory::GetOrCreate(board);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(BoardFactoryTest, InvalidBoardTypeIsRejected) {
  robot::board::Board board = MakeMockBoard("bad", 1);
  board.set_board_type(robot::board::BoardType::BOARD_INVALID);
  auto result = BoardFactory::GetOrCreate(board);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(BoardFactoryTest, UnportedBoardTypesReportUnimplemented) {
  robot::board::Board board = MakeMockBoard("am243_1", 1);
  board.set_board_type(robot::board::BoardType::AM243);
  auto result = BoardFactory::GetOrCreate(board);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kUnimplemented);
}

TEST_F(BoardFactoryTest, SameNameDifferentTypeIsRejected) {
  auto board_a = BoardFactory::GetOrCreate(MakeMockBoard("bridge_1", 1));
  ASSERT_TRUE(board_a.ok()) << board_a.status();
  robot::board::Board conflicting = MakeMockBoard("bridge_1", 1);
  conflicting.set_board_type(robot::board::BoardType::AM243);
  auto board_b = BoardFactory::GetOrCreate(conflicting);
  EXPECT_EQ(board_b.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(BoardFactoryTest, DuplicateChannelIndexIsRejected) {
  robot::board::Board board = MakeMockBoard("dup", 1);
  auto* channel = board.add_channels();
  channel->set_index(0);
  channel->set_drive(robot::board::DriveInterface::STEP_DIR);
  auto result = BoardFactory::GetOrCreate(board);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(BoardFactoryTest, OpenChannelAndRoundTripTarget) {
  auto board = BoardFactory::GetOrCreate(MakeMockBoard("bridge_1", 2));
  ASSERT_TRUE(board.ok()) << board.status();

  auto channel = (*board)->OpenChannel(1);
  ASSERT_TRUE(channel.ok()) << channel.status();

  // Targets are rejected until the channel is enabled.
  EXPECT_EQ((*channel)->SetTarget(TargetMode::kPosition, 4500.0f).code(),
            absl::StatusCode::kFailedPrecondition);

  ASSERT_TRUE((*channel)->Enable().ok());
  ASSERT_TRUE((*channel)->SetTarget(TargetMode::kPosition, 4500.0f).ok());
  auto feedback = (*channel)->ReadFeedback();
  ASSERT_TRUE(feedback.ok()) << feedback.status();
  EXPECT_FLOAT_EQ(feedback->position, 4500.0f);

  // The mock board does not support torque targets (capability, not subclass).
  EXPECT_EQ((*channel)->SetTarget(TargetMode::kTorque, 1.0f).code(),
            absl::StatusCode::kUnimplemented);
}

TEST_F(BoardFactoryTest, OpenChannelOutOfRangeIsNotFound) {
  auto board = BoardFactory::GetOrCreate(MakeMockBoard("bridge_1", 1));
  ASSERT_TRUE(board.ok()) << board.status();
  auto channel = (*board)->OpenChannel(7);
  EXPECT_EQ(channel.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(BoardFactoryTest, ResetForTestingDropsCache) {
  auto board_a = BoardFactory::GetOrCreate(MakeMockBoard("bridge_1", 1));
  ASSERT_TRUE(board_a.ok()) << board_a.status();
  BoardFactory::ResetForTesting();
  auto board_b = BoardFactory::GetOrCreate(MakeMockBoard("bridge_1", 1));
  ASSERT_TRUE(board_b.ok()) << board_b.status();
  EXPECT_NE(board_a->get(), board_b->get());
}

}  // namespace
}  // namespace robot::board
