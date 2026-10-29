"""Metadata invariants and executable conformance examples, not adapter enforcement."""

import asyncio
import os
import subprocess
import sys
import unittest
from dataclasses import FrozenInstanceError, dataclass, replace
from typing import Callable, Generic, Optional, TypeVar

from tools.mcp.observation_source import (
    Availability,
    ObservationLimits,
    ObservationResult,
    ObservationSource,
    ObservationTime,
    SourceDescriptor,
    UnavailableReason,
)

T = TypeVar("T")
CLOCK_ID = "test-monotonic:boot-1:namespace-1"
RECEIPT = ObservationTime(1_000_000_000, CLOCK_ID)
SOURCE_TIME = ObservationTime(-12, None)


@dataclass(frozen=True)
class NumericReading:
    value: float


@dataclass(frozen=True)
class ButtonState:
    pressed: bool


PAYLOADS = (NumericReading(2.5), ButtonState(True))


class FakeObservationSource(ObservationSource[T], Generic[T]):
    """Cooperative fixture only; real adapters must implement their own bounds."""

    def __init__(self, payload: T, clock: Callable[[], int], limits=None):
        self.descriptor = SourceDescriptor(
            "sensor:fixture",
            "fixture",
            "test.fixture/v1",
            limits=limits or ObservationLimits(),
        )
        self.sample: Optional[T] = payload
        self.payload_type = type(
            payload
        )  # Fixture-specific validation, never T inspection.
        self.payload_bytes = 8
        self.receipt_time = RECEIPT
        self.source_time = SOURCE_TIME
        self.clock = clock
        self.backend_error = False
        self.entered = asyncio.Event()
        self.release = asyncio.Event()
        self.release.set()
        self.closed = asyncio.Event()
        self.queries = set()
        self.query_calls = 0

    def describe(self) -> SourceDescriptor:
        return self.descriptor

    def unavailable(self, reason: UnavailableReason) -> ObservationResult[T]:
        return ObservationResult(
            self.descriptor.source_id,
            Availability.UNAVAILABLE,
            unavailable_reason=reason,
            diagnostic=f"Fixture outcome: {reason.value}.",
        )

    async def query(self):
        self.query_calls += 1
        self.entered.set()
        await self.release.wait()
        if self.backend_error:
            raise OSError("fixture backend failure")

    async def read_latest(self) -> ObservationResult[T]:
        if self.closed.is_set():
            return self.unavailable(UnavailableReason.CLOSED)
        limits = self.descriptor.limits
        if len(self.queries) >= limits.max_in_flight_reads:
            return self.unavailable(UnavailableReason.BUSY)
        query = asyncio.create_task(self.query())
        self.queries.add(query)
        closed = asyncio.create_task(self.closed.wait())
        try:
            done, _ = await asyncio.wait(
                (query, closed),
                timeout=limits.read_timeout_seconds,
                return_when=asyncio.FIRST_COMPLETED,
            )
            if self.closed.is_set():
                return self.unavailable(UnavailableReason.CLOSED)
            if query not in done:
                return self.unavailable(UnavailableReason.TIMEOUT)
            try:
                query.result()
            except OSError:
                return self.unavailable(UnavailableReason.BACKEND_ERROR)
            if self.sample is None:
                return self.unavailable(UnavailableReason.NO_DATA)
            if (
                not isinstance(self.sample, self.payload_type)
                or self.payload_bytes > limits.max_payload_bytes
                or self.receipt_time is None
                or self.receipt_time.clock_id != CLOCK_ID
            ):
                return self.unavailable(UnavailableReason.INVALID_DATA)
            age_ns = self.clock() - self.receipt_time.nanoseconds
            if age_ns < 0:
                return self.unavailable(UnavailableReason.INVALID_DATA)
            return ObservationResult(
                self.descriptor.source_id,
                (
                    Availability.STALE
                    if age_ns > limits.freshness_seconds * 1e9
                    else Availability.FRESH
                ),
                payload=self.sample,
                receipt_time=self.receipt_time,
                source_time=self.source_time,
                runtime_instance="fixture-runtime",
                sample_sequence=7,
            )
        finally:
            for task in (query, closed):
                if not task.done():
                    task.cancel()
            await asyncio.gather(query, closed, return_exceptions=True)
            self.queries.discard(query)

    async def aclose(self) -> None:
        self.closed.set()
        queries = tuple(self.queries)
        for query in queries:
            query.cancel()
        await asyncio.gather(*queries, return_exceptions=True)


async def read_source(source: ObservationSource[T]) -> ObservationResult[T]:
    """One caller for both payload kinds, without kind or diagnostic switches."""
    result = await source.read_latest()
    if result.source_id != source.describe().source_id:
        raise ValueError("source binding mismatch")
    return result


class ObservationMetadataTest(unittest.TestCase):
    def fresh(self, **changes):
        values = dict(
            source_id="sensor:example",
            availability=Availability.FRESH,
            payload=NumericReading(2.5),
            receipt_time=RECEIPT,
        )
        values.update(changes)
        return ObservationResult(**values)

    def test_descriptor_needs_only_identity_kind_and_schema(self):
        descriptor = SourceDescriptor("sensor:example", "numeric", "example.numeric/v1")
        self.assertEqual(descriptor.limits, ObservationLimits(10, 3, 65536, 1))
        self.assertIsNone(descriptor.encoding)
        self.assertIsNone(descriptor.units)
        self.assertIsNone(descriptor.reference)

    def test_policies_reject_nonfinite_nonpositive_and_wrong_types(self):
        for name in ("read_timeout_seconds", "freshness_seconds"):
            for value in (0, -1, float("nan"), float("inf"), True, "3", 10**1000):
                with self.subTest(name=name, value_type=type(value).__name__):
                    with self.assertRaises(ValueError):
                        ObservationLimits(**{name: value})
        for name in ("max_payload_bytes", "max_in_flight_reads"):
            for value in (0, -1, True, 1.5, "1"):
                with self.subTest(name=name, value=value):
                    with self.assertRaises(ValueError):
                        ObservationLimits(**{name: value})

    def test_descriptor_text_is_bounded_nonempty_utf8(self):
        for field in (
            "source_id",
            "kind",
            "schema_name",
            "encoding",
            "units",
            "reference",
        ):
            for value in ("", " ", "\x00", "é" * 257, "\ud800", 3):
                values = dict(
                    source_id="sensor:example", kind="numeric", schema_name="v1"
                )
                values[field] = value
                with self.subTest(field=field, value_type=type(value).__name__):
                    with self.assertRaises(ValueError):
                        SourceDescriptor(**values)
        with self.assertRaises(ValueError):
            SourceDescriptor("sensor:example", "numeric", "v1", limits="unbounded")

    def test_source_time_preserves_signed_nanoseconds_and_unknown_clock(self):
        for nanoseconds in (-12, 0, 12):
            stamp = ObservationTime(nanoseconds)
            self.assertEqual(stamp.nanoseconds, nanoseconds)
            self.assertIsNone(stamp.clock_id)
        self.assertEqual(self.fresh(source_time=SOURCE_TIME).source_time, SOURCE_TIME)

    def test_time_rejects_bool_fractional_and_invalid_clock(self):
        for nanoseconds in (True, 1.5, "12"):
            with self.assertRaises(ValueError):
                ObservationTime(nanoseconds)
        for clock in ("", " ", "\x00", "x" * 513, 3):
            with self.assertRaises(ValueError):
                ObservationTime(12, clock)

    def test_available_states_require_payload_and_known_receipt_clock(self):
        for availability in (Availability.FRESH, Availability.STALE):
            self.assertEqual(
                self.fresh(availability=availability).availability, availability
            )
            for changes in (
                dict(payload=None),
                dict(receipt_time=None),
                dict(receipt_time=ObservationTime(12)),
                dict(unavailable_reason=UnavailableReason.BUSY),
            ):
                with self.subTest(availability=availability, changes=changes):
                    with self.assertRaises(ValueError):
                        self.fresh(availability=availability, **changes)

    def test_unavailable_requires_reason_diagnostic_and_no_payload(self):
        values = dict(
            source_id="sensor:example",
            availability=Availability.UNAVAILABLE,
            unavailable_reason=UnavailableReason.NO_DATA,
            diagnostic="No sample yet.",
        )
        for reason in UnavailableReason:
            self.assertIs(
                ObservationResult(
                    **dict(values, unavailable_reason=reason)
                ).unavailable_reason,
                reason,
            )
        for changes in (
            dict(unavailable_reason=None),
            dict(unavailable_reason="busy"),
            dict(diagnostic=""),
            dict(diagnostic=" "),
            dict(payload=NumericReading(1)),
        ):
            with self.subTest(changes=changes):
                with self.assertRaises(ValueError):
                    ObservationResult(**dict(values, **changes))

    def test_result_rejects_invalid_metadata(self):
        for changes in (
            dict(source_id=""),
            dict(availability="fresh"),
            dict(sample_sequence=-1),
            dict(sample_sequence=True),
            dict(sample_sequence=1.5),
            dict(runtime_instance=""),
            dict(runtime_instance=3),
            dict(receipt_time=12),
            dict(source_time=12),
            dict(diagnostic="x" * 4097),
            dict(diagnostic="\ud800"),
        ):
            with self.subTest(changes=changes):
                with self.assertRaises(ValueError):
                    self.fresh(**changes)
        self.assertIsNone(self.fresh().sample_sequence)
        self.assertEqual(self.fresh(sample_sequence=0).sample_sequence, 0)

    def test_metadata_records_are_frozen(self):
        records = (
            (ObservationLimits(), "max_payload_bytes"),
            (SourceDescriptor("sensor:example", "numeric", "v1"), "source_id"),
            (RECEIPT, "nanoseconds"),
            (self.fresh(), "payload"),
        )
        for record, field in records:
            with self.subTest(record=type(record).__name__):
                with self.assertRaises(FrozenInstanceError):
                    setattr(record, field, None)

    def test_incomplete_source_cannot_be_instantiated(self):
        class DescribeOnly(ObservationSource):
            def describe(self):
                return SourceDescriptor("sensor:example", "numeric", "v1")

        with self.assertRaises(TypeError):
            ObservationSource()
        with self.assertRaises(TypeError):
            DescribeOnly()

    def test_import_needs_no_sdk_or_ros(self):
        code = (
            "import sys; import tools.mcp.observation_source; "
            "assert not any("
            "name.split('.')[0] in ('mcp', 'rclpy') for name in sys.modules)"
        )
        env = dict(os.environ, PYTHONPATH=os.pathsep.join(sys.path))
        result = subprocess.run(
            [sys.executable, "-c", code],
            env=env,
            capture_output=True,
            text=True,
            timeout=5,
        )
        self.assertEqual(result.returncode, 0, result.stderr)


class FakeSourceConformanceTest(unittest.IsolatedAsyncioTestCase):
    """Executable contract examples for one cooperative fixture."""

    def source(self, payload, **limits):
        self.now_ns = RECEIPT.nanoseconds
        source = FakeObservationSource(
            payload,
            lambda: self.now_ns,
            ObservationLimits(**limits),
        )
        self.addAsyncCleanup(source.aclose)
        return source

    async def pending_read(self, source):
        source.release.clear()
        pending = asyncio.create_task(read_source(source))
        await asyncio.wait_for(source.entered.wait(), 1)
        return pending

    async def assert_reason(self, source, reason):
        result = await asyncio.wait_for(read_source(source), 1)
        self.assertIs(result.availability, Availability.UNAVAILABLE)
        self.assertIs(result.unavailable_reason, reason)
        self.assertIsNone(result.payload)
        self.assertTrue(result.diagnostic)

    async def test_cached_describe_does_not_query_and_survives_close(self):
        for payload in PAYLOADS:
            source = self.source(payload)
            descriptor = source.describe()
            self.assertIs(descriptor, source.describe())
            self.assertEqual(source.query_calls, 0)
            await source.aclose()
            self.assertIs(descriptor, source.describe())
            self.assertEqual(source.query_calls, 0)

    async def test_latest_repeats_identity_and_becomes_stale_without_restamping(self):
        for payload in PAYLOADS:
            with self.subTest(payload=type(payload).__name__):
                source = self.source(payload)
                first = await read_source(source)
                second = await read_source(source)
                self.assertEqual(first, second)
                self.assertIs(first.payload, payload)
                self.assertIs(first.availability, Availability.FRESH)
                self.now_ns += 4_000_000_000
                stale = await read_source(source)
                self.assertIs(stale.availability, Availability.STALE)
                self.assertEqual(
                    replace(stale, availability=Availability.FRESH),
                    first,
                )

    async def test_missing_and_invalid_data_have_typed_reasons_for_both_kinds(self):
        for payload in PAYLOADS:
            source = self.source(payload)
            source.sample = None
            await self.assert_reason(source, UnavailableReason.NO_DATA)
            source.sample = payload
            for receipt in (
                None,
                ObservationTime(12),
                ObservationTime(12, "other-clock"),
            ):
                source.receipt_time = receipt
                await self.assert_reason(source, UnavailableReason.INVALID_DATA)
            source.receipt_time = RECEIPT
            source.payload_bytes = source.describe().limits.max_payload_bytes + 1
            await self.assert_reason(source, UnavailableReason.INVALID_DATA)
            source.payload_bytes = 8
            source.sample = (
                object()
            )  # The fixture validates its own known payload type.
            await self.assert_reason(source, UnavailableReason.INVALID_DATA)

    async def test_busy_is_bounded_and_pending_read_recovers(self):
        for payload in PAYLOADS:
            source = self.source(payload)
            pending = await self.pending_read(source)
            await self.assert_reason(source, UnavailableReason.BUSY)
            self.assertEqual(len(source.queries), 1)
            self.assertEqual(source.query_calls, 1)
            source.release.set()
            self.assertIs(
                (await asyncio.wait_for(pending, 1)).availability, Availability.FRESH
            )
            self.assertFalse(source.queries)

    async def test_deadline_cleans_query_and_recovers(self):
        for payload in PAYLOADS:
            source = self.source(payload, read_timeout_seconds=0.02)
            source.release.clear()
            await self.assert_reason(source, UnavailableReason.TIMEOUT)
            self.assertFalse(source.queries)
            source.release.set()
            self.assertIs((await read_source(source)).availability, Availability.FRESH)

    async def test_caller_cancellation_cleans_query_and_propagates(self):
        for payload in PAYLOADS:
            source = self.source(payload)
            pending = await self.pending_read(source)
            pending.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await asyncio.wait_for(pending, 1)
            self.assertFalse(source.queries)
            source.release.set()
            self.assertIs((await read_source(source)).availability, Availability.FRESH)

    async def test_concurrent_close_returns_closed_without_cancelling_caller(self):
        for payload in PAYLOADS:
            source = self.source(payload)
            pending = await self.pending_read(source)
            await asyncio.wait_for(asyncio.gather(source.aclose(), source.aclose()), 1)
            result = await asyncio.wait_for(pending, 1)
            self.assertIs(result.unavailable_reason, UnavailableReason.CLOSED)
            self.assertFalse(pending.cancelled())
            self.assertFalse(source.queries)
            await source.aclose()
            await self.assert_reason(source, UnavailableReason.CLOSED)

    async def test_backend_failure_has_a_reason_and_recovers(self):
        for payload in PAYLOADS:
            source = self.source(payload)
            source.backend_error = True
            await self.assert_reason(source, UnavailableReason.BACKEND_ERROR)
            self.assertFalse(source.queries)
            source.backend_error = False
            self.assertIs((await read_source(source)).availability, Availability.FRESH)


if __name__ == "__main__":
    unittest.main()
