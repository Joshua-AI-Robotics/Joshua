#include "robot/comm/factory/transport_requirements.h"

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "robot/comm/proto/comm.pb.h"

namespace robot::comm {
namespace {

TEST(RequiredTransportsTest, SingleTransportTypeMapsToOneElementSet) {
  Comm comm;
  comm.set_transport_type(TransportType::MESSAGE);

  auto required = RequiredTransports(comm);

  ASSERT_TRUE(required.ok()) << required.status();
  EXPECT_EQ(*required, TransportSet{TransportType::MESSAGE});
}

TEST(RequiredTransportsTest, RepeatedFieldDeclaresSeveralCapabilities) {
  Comm comm;
  comm.add_required_transports(TransportType::CORRELATED_CYCLIC);
  comm.add_required_transports(TransportType::MESSAGE);

  auto required = RequiredTransports(comm);

  ASSERT_TRUE(required.ok()) << required.status();
  EXPECT_EQ(*required, (TransportSet{TransportType::MESSAGE, TransportType::CORRELATED_CYCLIC}));
}

TEST(RequiredTransportsTest, RejectsNoDeclaredTransport) {
  EXPECT_EQ(RequiredTransports(Comm{}).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(RequiredTransportsTest, RejectsBothForms) {
  Comm comm;
  comm.set_transport_type(TransportType::MESSAGE);
  comm.add_required_transports(TransportType::MESSAGE);

  EXPECT_EQ(RequiredTransports(comm).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(RequiredTransportsTest, RejectsDuplicateCapability) {
  Comm comm;
  comm.add_required_transports(TransportType::MESSAGE);
  comm.add_required_transports(TransportType::MESSAGE);

  EXPECT_EQ(RequiredTransports(comm).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(RequiredTransportsTest, RejectsInvalidCapabilityInRepeatedField) {
  Comm comm;
  comm.add_required_transports(TransportType::TRANSPORT_INVALID);

  EXPECT_EQ(RequiredTransports(comm).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(ExpectRequiredTransportsTest, AcceptsExactMatchFromEitherForm) {
  Comm single;
  single.set_transport_type(TransportType::MESSAGE);
  Comm repeated;
  repeated.add_required_transports(TransportType::MESSAGE);

  EXPECT_TRUE(ExpectRequiredTransports(single, {TransportType::MESSAGE}, "Board 'b'").ok());
  EXPECT_TRUE(ExpectRequiredTransports(repeated, {TransportType::MESSAGE}, "Board 'b'").ok());
}

TEST(ExpectRequiredTransportsTest, RejectsSupersetAndNamesOwner) {
  Comm comm;
  comm.add_required_transports(TransportType::MESSAGE);
  comm.add_required_transports(TransportType::BYTE_STREAM);

  auto status = ExpectRequiredTransports(comm, {TransportType::MESSAGE}, "Board 'b'");

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "Board 'b' requires MESSAGE transport, but its comm requires BYTE_STREAM+MESSAGE.");
}

}  // namespace
}  // namespace robot::comm
