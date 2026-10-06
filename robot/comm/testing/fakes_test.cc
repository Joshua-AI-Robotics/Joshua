#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "robot/comm/testing/fake_correlated_cyclic_transport.h"
#include "robot/comm/testing/fake_message_transport.h"

namespace robot::comm {
namespace {

TEST(FakeMessageTransportTest, ExchangeReturnsQueuedResponsesInOrderAndRecordsRequests) {
  FakeMessageTransport transport;
  transport.QueueResponse({0x01});
  transport.QueueResponse({0x02});
  const std::vector<uint8_t> request = {0xAA, 0xBB};

  auto first = transport.Exchange(request, absl::Milliseconds(5));
  auto second = transport.Exchange(request, absl::Milliseconds(7));

  ASSERT_TRUE(first.ok()) << first.status();
  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_EQ(*first, std::vector<uint8_t>{0x01});
  EXPECT_EQ(*second, std::vector<uint8_t>{0x02});
  EXPECT_EQ(transport.exchange_calls_, 2);
  EXPECT_EQ(transport.last_request_, request);
  EXPECT_EQ(transport.last_timeout_, absl::Milliseconds(7));
}

TEST(FakeMessageTransportTest, ExchangeWithoutQueuedResponseTimesOut) {
  FakeMessageTransport transport;
  const std::vector<uint8_t> request = {0xAA};

  EXPECT_EQ(transport.Exchange(request, absl::Milliseconds(5)).status().code(),
            absl::StatusCode::kDeadlineExceeded);
}

TEST(FakeMessageTransportTest, SendRecordsWithoutConsumingResponses) {
  FakeMessageTransport transport;
  transport.QueueResponse({0x01});
  const std::vector<uint8_t> request = {0xCC};

  ASSERT_TRUE(transport.Send(request, absl::Milliseconds(5)).ok());

  EXPECT_EQ(transport.send_calls_, 1);
  EXPECT_EQ(transport.exchange_calls_, 0);
  EXPECT_EQ(transport.last_request_, request);
  EXPECT_EQ(transport.queued_responses_.size(), 1u);
}

TEST(FakeCorrelatedCyclicTransportTest, ExchangeReturnsQueuedResponseOrTimesOut) {
  FakeCorrelatedCyclicTransport transport;
  transport.QueueResponse({0x10, 0x20});
  const std::vector<uint8_t> request = {0x03};

  auto response = transport.Exchange(request, absl::Milliseconds(1));
  auto timed_out = transport.Exchange(request, absl::Milliseconds(1));

  ASSERT_TRUE(response.ok()) << response.status();
  EXPECT_EQ(*response, (std::vector<uint8_t>{0x10, 0x20}));
  EXPECT_EQ(timed_out.status().code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_EQ(transport.exchange_calls_, 2);
}

}  // namespace
}  // namespace robot::comm
