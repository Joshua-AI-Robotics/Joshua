#include "firmware/common/joshua_stream_subscription.h"

#include <vector>

#include "firmware/common/joshua_wire_endpoint.h"
#include "gtest/gtest.h"

namespace {
using Bytes = std::vector<uint8_t>;

class StreamSubscriptionTest : public ::testing::Test {
 protected:
  jw_endpoint_t endpoint{};
  jw_stream_subscription_t subscription{};
  uint32_t session = 0x12345678;
  uint32_t next_id = 2;
  unsigned calls = 0;
  unsigned resets = 0;
  void SetUp() override {
    // Software fixture limits; these are not a physical sensor qualification.
    ASSERT_EQ(jw_stream_subscription_init(&subscription, 7, 1000, 1000000), JW_RESULT_OK);
    jw_endpoint_init(&endpoint);
    ASSERT_FALSE(Reset(session).empty());
  }
  static int Handle(void* context, const jw_command_t* command, uint8_t* out, size_t cap) {
    auto* self = static_cast<StreamSubscriptionTest*>(context);
    ++self->calls;
    return jw_stream_subscription_handle(&self->subscription, command, out, cap);
  }
  static void Clear(void* context) {
    auto* self = static_cast<StreamSubscriptionTest*>(context);
    ++self->resets;
    jw_stream_subscription_reset(&self->subscription);
  }
  Bytes Process(const Bytes& request) {
    Bytes reply(JW_MAX_FRAME_LEN);
    const int size = jw_endpoint_process(
        &endpoint, request.data(), request.size(), reply.data(), reply.size(), Handle, Clear, this);
    EXPECT_GE(size, 0);
    reply.resize(size > 0 ? size : 0);
    return reply;
  }
  Bytes Reset(uint32_t new_session) {
    session = new_session;
    next_id = 2;
    Bytes request(JW_MIN_FRAME_LEN);
    EXPECT_EQ(jw_encode_frame(request.data(),
                              request.size(),
                              session,
                              1,
                              JW_CMD_RESET_SESSION,
                              JW_CHANNEL_NONE,
                              nullptr,
                              0),
              request.size());
    return Process(request);
  }
  Bytes Request(uint8_t operation,
                const Bytes& data = {},
                uint8_t minor = 1,
                uint8_t function = 7) {
    uint8_t payload[JW_MAX_PAYLOAD_LEN];
    const jw_profile_request_t profile{
        JW_PROFILE_SENSOR, 1, minor, operation, data.data(), data.size()};
    const int size = jw_encode_profile_request(payload, sizeof(payload), &profile);
    EXPECT_GT(size, 0);
    Bytes request(JW_MAX_FRAME_LEN);
    const int length = jw_encode_frame(request.data(),
                                       request.size(),
                                       session,
                                       next_id++,
                                       JW_CMD_PROFILE,
                                       function,
                                       payload,
                                       size);
    EXPECT_GT(length, 0);
    request.resize(length);
    return request;
  }
  jw_reply_t Reply(const Bytes& bytes) {
    jw_frame_t frame{};
    EXPECT_EQ(jw_decode_frame(bytes.data(), bytes.size(), &frame), JW_RESULT_OK);
    jw_reply_t reply{};
    EXPECT_EQ(jw_decode_reply_payload(frame.payload, frame.payload_len, &reply), JW_RESULT_OK);
    return reply;  // Borrowed storage remains owned by bytes.
  }
  jw_stream_state_t State(const Bytes& bytes) {
    const auto reply = Reply(bytes);
    EXPECT_EQ(reply.status, JW_STATUS_OK);
    EXPECT_EQ(reply.execution, JW_EXECUTION_COMPLETE);
    jw_stream_state_t state{};
    EXPECT_EQ(jw_decode_stream_state(reply.data, reply.data_len, &state), JW_RESULT_OK);
    return state;
  }
  Bytes Value(uint32_t value) {
    Bytes bytes(4);
    EXPECT_EQ(jw_encode_stream_token(bytes.data(), bytes.size(), value), 4);
    return bytes;
  }
};

TEST_F(StreamSubscriptionTest, LostCreateAndCancelRepliesReplayWithoutRepeatingEffects) {
  const auto create = Request(JW_SENSOR_SUBSCRIBE, Value(10000));
  const auto created = Process(create);  // Pretend this reply was lost on the link.
  const auto count = calls;
  EXPECT_EQ(Process(create), created);
  EXPECT_EQ(calls, count);
  const auto active = State(created);
  EXPECT_EQ(active.active, 1);
  EXPECT_EQ(active.subscription_id, 2u);
  EXPECT_EQ(active.period_us, 10000u);
  const auto inspected = Process(Request(JW_SENSOR_GET_SUBSCRIPTION));
  EXPECT_EQ(State(inspected).subscription_id, active.subscription_id);
  const auto cancel = Request(JW_SENSOR_UNSUBSCRIBE, Value(active.subscription_id));
  const auto canceled = Process(cancel);
  EXPECT_EQ(State(canceled).active, 0);
  EXPECT_EQ(State(canceled).subscription_id, active.subscription_id);
  const auto cancel_count = calls;
  EXPECT_EQ(Process(cancel), canceled);
  EXPECT_EQ(calls, cancel_count);
  const auto reconciled = Process(Request(JW_SENSOR_GET_SUBSCRIPTION));
  EXPECT_EQ(State(reconciled).active, 0);
  EXPECT_EQ(State(reconciled).subscription_id, active.subscription_id);
}

TEST_F(StreamSubscriptionTest, NewCreateWhileActiveIsBusyAndOldCancelCannotCloseReplacement) {
  const auto first_reply = Process(Request(JW_SENSOR_SUBSCRIBE, Value(10000)));
  const auto first = State(first_reply);
  const auto busy = Process(Request(JW_SENSOR_SUBSCRIBE, Value(20000)));
  EXPECT_EQ(Reply(busy).status, JW_STATUS_BUSY);
  EXPECT_EQ(subscription.state.subscription_id, first.subscription_id);
  const auto canceled = Process(Request(JW_SENSOR_UNSUBSCRIBE, Value(first.subscription_id)));
  EXPECT_EQ(State(canceled).active, 0);
  const auto next_reply = Process(Request(JW_SENSOR_SUBSCRIBE, Value(20000)));
  const auto next = State(next_reply);
  EXPECT_GT(next.subscription_id, first.subscription_id);
  const auto stale_cancel = Process(Request(JW_SENSOR_UNSUBSCRIBE, Value(first.subscription_id)));
  EXPECT_EQ(Reply(stale_cancel).status, JW_STATUS_UNAVAILABLE);
  EXPECT_EQ(subscription.state.active, 1);
  EXPECT_EQ(subscription.state.subscription_id, next.subscription_id);
  jw_stream_sample_t sample{session, first.subscription_id, 7, 0, {}};
  EXPECT_FALSE(jw_stream_sample_matches(&sample, session, 7, &next));
  sample.subscription_id = next.subscription_id;
  EXPECT_TRUE(jw_stream_sample_matches(&sample, session, 7, &next));
}

TEST_F(StreamSubscriptionTest, ResetInvalidatesStreamsEvenWhenNumericTokensRepeat) {
  const auto created = Process(Request(JW_SENSOR_SUBSCRIBE, Value(10000)));
  const auto old = State(created);
  jw_stream_sample_t sample{session, old.subscription_id, 7, 0, {}};
  EXPECT_TRUE(jw_stream_sample_matches(&sample, session, 7, &old));
  ASSERT_FALSE(Reset(session + 1).empty());
  EXPECT_EQ(subscription.state.active, 0);
  EXPECT_EQ(subscription.state.subscription_id, 0u);
  const auto next_reply = Process(Request(JW_SENSOR_SUBSCRIBE, Value(10000)));
  const auto next = State(next_reply);
  EXPECT_EQ(next.subscription_id, old.subscription_id);
  EXPECT_FALSE(jw_stream_sample_matches(&sample, session, 7, &next));
  sample.session_id = session;
  EXPECT_TRUE(jw_stream_sample_matches(&sample, session, 7, &next));
}

TEST_F(StreamSubscriptionTest, NotificationInputCannotConsumeRequestIdsOrReplayCache) {
  const auto create = Request(JW_SENSOR_SUBSCRIBE, Value(10000));
  const auto reply = Process(create);
  jw_stream_sample_t sample{
      session, UINT32_MAX, 7, 0, {1, {JW_SAMPLE_VALID, JW_CLOCK_BOOT_MONOTONIC_US, 1, 1, 1}}};
  Bytes event(JW_STREAM_SAMPLE_FRAME_LEN);
  ASSERT_EQ(jw_encode_stream_sample(event.data(), event.size(), &sample), event.size());
  const auto count = calls;
  EXPECT_TRUE(Process(event).empty());
  EXPECT_EQ(calls, count);
  EXPECT_EQ(Process(create), reply);
  const auto query = Process(Request(JW_SENSOR_GET_SUBSCRIPTION));
  EXPECT_EQ(State(query).active, 1);
}

TEST_F(StreamSubscriptionTest, InvalidRequestsDoNotMutateSubscription) {
  const auto empty = Process(Request(JW_SENSOR_GET_SUBSCRIPTION));
  EXPECT_EQ(State(empty).active, 0);
  EXPECT_EQ(subscription.session_id, 0u);  // Even internal binding stays read-only.
  for (uint32_t period : {999u, 1000001u}) {
    const auto reply = Process(Request(JW_SENSOR_SUBSCRIBE, Value(period)));
    EXPECT_EQ(Reply(reply).status, JW_STATUS_INVALID_PAYLOAD);
  }
  for (uint8_t minor : {0, 2}) {
    const auto reply = Process(Request(JW_SENSOR_SUBSCRIBE, Value(10000), minor));
    EXPECT_EQ(Reply(reply).status, JW_STATUS_PROFILE_MISMATCH);
  }
  const auto wrong_function = Process(Request(JW_SENSOR_SUBSCRIBE, Value(10000), 1, 8));
  EXPECT_EQ(Reply(wrong_function).status, JW_STATUS_INVALID_CHANNEL);
  const auto bad_query = Process(Request(JW_SENSOR_GET_SUBSCRIPTION, Value(1)));
  EXPECT_EQ(Reply(bad_query).status, JW_STATUS_INVALID_PAYLOAD);
  const auto missing_period = Process(Request(JW_SENSOR_SUBSCRIBE));
  EXPECT_EQ(Reply(missing_period).status, JW_STATUS_INVALID_PAYLOAD);
  EXPECT_EQ(subscription.state.active, 0);
  EXPECT_EQ(subscription.state.subscription_id, 0u);
}

TEST_F(StreamSubscriptionTest, CapacityPreflightAndSessionBindingPreventUntrackedMutation) {
  const auto request = Request(JW_SENSOR_SUBSCRIBE, Value(10000));
  jw_frame_t frame{};
  ASSERT_EQ(jw_decode_frame(request.data(), request.size(), &frame), JW_RESULT_OK);
  jw_command_t command{frame.session_id,
                       frame.message_id,
                       frame.cmd,
                       frame.channel,
                       frame.payload,
                       frame.payload_len};
  Bytes reply(JW_REPLY_HEADER_LEN + JW_STREAM_STATE_LEN, 0xcc);
  for (size_t cap = 0; cap < reply.size(); ++cap) {
    EXPECT_EQ(jw_stream_subscription_handle(&subscription, &command, reply.data(), cap), -1);
    EXPECT_EQ(reply, Bytes(reply.size(), 0xcc));
    EXPECT_EQ(subscription.state.active, 0);
    EXPECT_EQ(subscription.session_id, 0u);
  }
  ASSERT_GT(jw_stream_subscription_handle(&subscription, &command, reply.data(), reply.size()), 0);
  const auto token = subscription.state.subscription_id;
  ++command.session_id;
  EXPECT_GT(jw_stream_subscription_handle(&subscription, &command, reply.data(), reply.size()), 0);
  jw_reply_t decoded{};
  ASSERT_EQ(jw_decode_reply_payload(reply.data(), JW_REPLY_HEADER_LEN, &decoded), JW_RESULT_OK);
  EXPECT_EQ(decoded.status, JW_STATUS_UNAVAILABLE);
  EXPECT_EQ(subscription.state.subscription_id, token);
  EXPECT_EQ(jw_stream_subscription_init(nullptr, 7, 1, 2), JW_RESULT_ERROR);
  EXPECT_EQ(jw_stream_subscription_init(&subscription, JW_CHANNEL_NONE, 1, 2), JW_RESULT_ERROR);
  EXPECT_EQ(jw_stream_subscription_init(&subscription, 7, 0, 2), JW_RESULT_ERROR);
  EXPECT_EQ(jw_stream_subscription_init(&subscription, 7, 2, 1), JW_RESULT_ERROR);
  EXPECT_EQ(subscription.state.subscription_id, token);
}
}  // namespace
