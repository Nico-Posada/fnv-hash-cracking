from dataclasses import dataclass
from enum import IntEnum


class CrackStatus(IntEnum):
    INTERRUPTED = -6
    BAD_SEARCH_LENGTH = -5
    CONTEXT_UNINITIALIZED = -4
    MISSING_BRUTE_CHARS = -3
    MEMORY_ERROR = -2
    FAILED = -1
    SUCCESS = 0


@dataclass(frozen=True)
class CrackResult:
    status: int
    value: bytes | None

    @property
    def ok(self) -> bool:
        return self.status == CrackStatus.SUCCESS

    @property
    def status_name(self) -> str:
        try:
            return CrackStatus(self.status).name
        except ValueError:
            return "UNKNOWN"
