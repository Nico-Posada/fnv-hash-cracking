from dataclasses import dataclass
from enum import IntEnum


class CrackStatus(IntEnum):
    """Status code returned by a crack attempt."""

    INTERRUPTED = -6
    """The search was interrupted before completion."""

    BAD_SEARCH_LENGTH = -5
    """The requested search length is invalid."""

    CONTEXT_UNINITIALIZED = -4
    """The native context was not initialized."""

    MISSING_BRUTE_CHARS = -3
    """A brute force search was requested without brute force characters."""

    MEMORY_ERROR = -2
    """Native code failed to allocate required memory."""

    FAILED = -1
    """No matching input was found."""

    SUCCESS = 0
    """A matching input was found."""


@dataclass(frozen=True)
class CrackResult:
    """Result returned by :meth:`fnvcrack.CrackContext.crack`.

    :ivar status: Numeric crack status.
    :ivar value: Cracked bytes when successful, otherwise ``None``.
    """

    status: int
    value: bytes | None

    @property
    def ok(self) -> bool:
        """Whether the crack completed successfully."""
        return self.status == CrackStatus.SUCCESS

    @property
    def status_name(self) -> str:
        """Human-readable name for :attr:`status`."""
        try:
            return CrackStatus(self.status).name
        except ValueError:
            return "UNKNOWN"
