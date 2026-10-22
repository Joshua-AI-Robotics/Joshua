// Maintained interface/fake contract checks; no serial, SOEM or device access.
// Protocol correlation and cyclic scheduling need separate adapter tests.
#include <memory>
#include <type_traits>

#include "gtest/gtest.h"
#include "robot/comm/testing/fake_caller_sized_message_transport.h"
#include "robot/comm/testing/fake_transports.h"

namespace robot::comm::testing {
namespace {
// Modern implementations provide only Send/Exchange (or cyclic Exchange),
// without implementing caller_sized fixed-length or transport-open operations.
static_assert(!std::is_abstract_v<FakeMessageTransport>);
static_assert(!std::is_abstract_v<FakeCorrelatedCyclicTransport>);
static_assert(!std::is_base_of_v<CallerSizedMessageTransport, FakeMessageTransport>);
static_assert(!std::is_base_of_v<MessageTransport, FakeCorrelatedCyclicTransport>);

TEST(TransportContract, SendDoesNotConsumeAnExchangeResult) {
  FakeMessageTransport fake;
  MessageTransport& message = fake;
  fake.results.push_back(Bytes{8, 9});
  Bytes request{1, 2, 3};
  ASSERT_TRUE(message.Send(absl::MakeConstSpan(request).subspan(1)).ok());
  request[1] = 99;
  EXPECT_EQ(fake.sent, (std::vector<Bytes>{{2, 3}}));
  ASSERT_EQ(fake.results.size(), 1);
  auto reply = message.Exchange(Bytes{4});
  ASSERT_TRUE(reply.ok());
  EXPECT_EQ(*reply, (Bytes{8, 9}));
  EXPECT_EQ(fake.exchanged, (std::vector<Bytes>{{4}}));
  EXPECT_TRUE(fake.results.empty());
}

TEST(TransportContract, MessageErrorsPropagateWithoutInventedResponses) {
  FakeMessageTransport fake;
  fake.send_status = absl::UnavailableError("link lost");
  EXPECT_EQ(fake.Send(Bytes{1}).code(), absl::StatusCode::kUnavailable);
  fake.results.push_back(absl::DeadlineExceededError("outcome unknown"));
  const auto reply = fake.Exchange(Bytes{2});
  EXPECT_EQ(reply.status().code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_EQ(reply.status().message(), "outcome unknown");
  EXPECT_EQ(fake.Exchange(Bytes{3}).status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(TransportContract, CyclicOwnsRecordedBytesAndPropagatesTimeoutAndStop) {
  FakeCorrelatedCyclicTransport fake;
  CorrelatedCyclicTransport& cyclic = fake;
  fake.results.push_back(Bytes{9});
  fake.results.push_back(absl::DeadlineExceededError("published; outcome unknown"));
  fake.results.push_back(absl::CancelledError("stopped"));
  Bytes request{1, 2};
  const auto reply = cyclic.Exchange(request, absl::Milliseconds(3));
  ASSERT_TRUE(reply.ok());
  request[0] = 99;
  EXPECT_EQ(fake.exchanged.front(), (Bytes{1, 2}));
  EXPECT_EQ(*reply, (Bytes{9}));
  EXPECT_EQ(cyclic.Exchange(request, absl::Milliseconds(5)).status().code(),
            absl::StatusCode::kDeadlineExceeded);
  EXPECT_EQ(cyclic.Exchange(request, absl::Milliseconds(7)).status().code(),
            absl::StatusCode::kCancelled);
  EXPECT_EQ(fake.timeouts,
            (std::vector<absl::Duration>{
                absl::Milliseconds(3), absl::Milliseconds(5), absl::Milliseconds(7)}));
  EXPECT_EQ(cyclic.Exchange(request, absl::Milliseconds(1)).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST(TransportContract, CyclicRejectsInvalidTimeoutBeforeRecordingWork) {
  FakeCorrelatedCyclicTransport fake;
  fake.results.push_back(Bytes{1});
  for (auto timeout : {absl::ZeroDuration(), absl::Milliseconds(-1), absl::InfiniteDuration()}) {
    EXPECT_EQ(fake.Exchange(Bytes{2}, timeout).status().code(), absl::StatusCode::kInvalidArgument);
  }
  EXPECT_TRUE(fake.exchanged.empty());
  EXPECT_TRUE(fake.timeouts.empty());
  EXPECT_EQ(fake.results.size(), 1);
}

TEST(TransportContract, CallerSizedIsExplicitAndCannotMasqueradeAsFramedExchange) {
  auto modern = std::make_shared<FakeMessageTransport>();
  EXPECT_EQ(GetCallerSizedMessageTransport(modern).status().code(),
            absl::StatusCode::kFailedPrecondition);
  auto caller_sized = std::make_shared<FakeCallerSizedMessageTransport>();
  ASSERT_TRUE(GetCallerSizedMessageTransport(caller_sized).ok());
  MessageTransport& message = *caller_sized;
  ASSERT_TRUE(message.Send(Bytes{4, 5}).ok());
  EXPECT_EQ(caller_sized->last_written_, (Bytes{4, 5}));
  EXPECT_EQ(message.Exchange(Bytes{6}).status().code(), absl::StatusCode::kUnimplemented);
  EXPECT_EQ(caller_sized->exchange_calls_, 0);
  caller_sized->QueueResponse({7});
  const auto reply = caller_sized->SendAndReceive({6}, 1);
  ASSERT_TRUE(reply.ok());
  EXPECT_EQ(*reply, (Bytes{7}));
}
}  // namespace
}  // namespace robot::comm::testing
