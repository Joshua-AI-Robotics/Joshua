"""Typed observation metadata and an adapter-owned reader contract.

Only metadata invariants are enforced here. Concrete adapters validate payload
type/size/ownership, source binding, clock comparability, freshness, deadlines,
concurrency and cleanup. Generic typing does not validate runtime payloads.
"""

import math
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from enum import Enum
from typing import Generic, Optional, TypeVar

T = TypeVar("T")


def _text(value: str, name: str, *, allow_empty=False, maximum=512) -> None:
    if type(value) is not str:
        raise ValueError(f"{name} must be text")
    try:
        size = len(value.encode("utf-8"))
    except UnicodeEncodeError as error:
        raise ValueError(f"{name} must be UTF-8 text") from error
    if size > maximum or "\x00" in value or (not allow_empty and not value.strip()):
        raise ValueError(f"{name} must be nonempty bounded text without NUL")


def _optional_text(value: Optional[str], name: str) -> None:
    if value is not None:
        _text(value, name)


class Availability(str, Enum):
    """Adapter-assessed receipt freshness; acquisition freshness is not implied."""

    FRESH = "fresh"
    STALE = "stale"
    UNAVAILABLE = "unavailable"


class UnavailableReason(str, Enum):
    """Outcome identity only; codes do not promise retry safety."""

    NO_DATA = "no_data"
    BUSY = "busy"
    TIMEOUT = "timeout"
    CLOSED = "closed"
    INVALID_DATA = "invalid_data"
    BACKEND_ERROR = "backend_error"


@dataclass(frozen=True)
class ObservationLimits:
    """Binding policies for adapters to enforce; defaults fit small observations."""

    read_timeout_seconds: float = 10.0
    freshness_seconds: float = 3.0
    max_payload_bytes: int = 64 * 1024
    max_in_flight_reads: int = 1

    def __post_init__(self) -> None:
        for name in ("read_timeout_seconds", "freshness_seconds"):
            value = getattr(self, name)
            try:
                valid = (
                    type(value) in (int, float) and value > 0 and math.isfinite(value)
                )
            except OverflowError:
                valid = False
            if not valid:
                raise ValueError(f"{name} must be positive and finite")
        for name in ("max_payload_bytes", "max_in_flight_reads"):
            value = getattr(self, name)
            if type(value) is not int or value <= 0:
                raise ValueError(f"{name} must be a positive integer")


@dataclass(frozen=True)
class SourceDescriptor:
    """Cached configured identity and format, without wiring or calibration copies."""

    source_id: str
    kind: str
    schema_name: str
    encoding: Optional[str] = None
    units: Optional[str] = None
    reference: Optional[str] = None
    limits: ObservationLimits = field(default_factory=ObservationLimits)

    def __post_init__(self) -> None:
        for name in ("source_id", "kind", "schema_name"):
            _text(getattr(self, name), name)
        for name in ("encoding", "units", "reference"):
            _optional_text(getattr(self, name), name)
        if not isinstance(self.limits, ObservationLimits):
            raise ValueError("limits must be ObservationLimits")


@dataclass(frozen=True)
class ObservationTime:
    """Signed nanoseconds; None clock_id means an unknown clock origin.

    A known local receipt clock must identify its comparable origin, including
    boot/time namespace where applicable. A name alone cannot verify synchronization.
    """

    nanoseconds: int
    clock_id: Optional[str] = None

    def __post_init__(self) -> None:
        if type(self.nanoseconds) is not int:
            raise ValueError("nanoseconds must be an integer")
        _optional_text(self.clock_id, "clock_id")


@dataclass(frozen=True)
class ObservationResult(Generic[T]):
    """One retained sample or a typed unavailable outcome.

    Repeated reads preserve sample identity and timestamps. Frozen fields do not
    deep-freeze payloads: adapters supply immutable values or independent owned
    copies. Metadata validation cannot establish acquisition freshness.
    """

    source_id: str
    availability: Availability
    payload: Optional[T] = None
    receipt_time: Optional[ObservationTime] = None
    source_time: Optional[ObservationTime] = None
    runtime_instance: Optional[str] = None
    sample_sequence: Optional[int] = None
    diagnostic: str = ""
    unavailable_reason: Optional[UnavailableReason] = None

    def __post_init__(self) -> None:
        _text(self.source_id, "source_id")
        if not isinstance(self.availability, Availability):
            raise ValueError("availability must be Availability")
        _text(self.diagnostic, "diagnostic", allow_empty=True, maximum=4096)
        _optional_text(self.runtime_instance, "runtime_instance")
        for name in ("receipt_time", "source_time"):
            stamp = getattr(self, name)
            if stamp is not None and not isinstance(stamp, ObservationTime):
                raise ValueError(f"{name} must be ObservationTime or None")
        if self.sample_sequence is not None and (
            type(self.sample_sequence) is not int or self.sample_sequence < 0
        ):
            raise ValueError("sample_sequence must be a nonnegative integer or None")
        if self.availability is Availability.UNAVAILABLE:
            if (
                self.payload is not None
                or not isinstance(self.unavailable_reason, UnavailableReason)
                or not self.diagnostic.strip()
            ):
                raise ValueError(
                    "unavailable results need a reason, diagnostic and no payload"
                )
        elif (
            self.payload is None
            or self.receipt_time is None
            or self.receipt_time.clock_id is None
            or self.unavailable_reason is not None
        ):
            raise ValueError(
                "fresh/stale results need a payload, known receipt clock and no reason"
            )


class ObservationSource(ABC, Generic[T]):
    """One configured, enabled source; implementations own all query enforcement.

    Implement three methods and compose transport helpers outside this base.
    Reader cleanup never stops or commands the runtime or device. An ABC cannot
    enforce these obligations for subclasses; each adapter needs conformance tests.
    """

    @abstractmethod
    def describe(self) -> SourceDescriptor:
        """Return stable cached metadata, including after close, without discovery."""
        ...

    @abstractmethod
    async def read_latest(self) -> ObservationResult[T]:
        """Non-destructively return the latest bounded sample or unavailability.

        The adapter enforces its fixed deadline, payload and concurrency limits.
        A repeat can return the same sample; reads must not mint sample identity
        or restamp receipt/source times. Caller cancellation propagates after query
        cleanup. Closing a pending query yields UNAVAILABLE/CLOSED.
        """
        ...

    @abstractmethod
    async def aclose(self) -> None:
        """Idempotently close reader resources, including source-owned queries.

        Do not cancel consumer-owned tasks. Pending and subsequent reads return
        UNAVAILABLE/CLOSED; concurrent close calls are safe and cannot revive it.
        """
        ...
