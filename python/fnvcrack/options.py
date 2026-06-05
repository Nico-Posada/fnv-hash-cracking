from dataclasses import dataclass
from enum import IntEnum, StrEnum


DEFAULT_ENUM_BOUND = 4
DEFAULT_MAX_ENUM_CANDIDATES = 0


class CrackStrategy(StrEnum):
    LLL = "lll"
    ENUMERATE = "enumerate"


class _NativeStrategy(IntEnum):
    LLL = 0
    ENUMERATE = 1


@dataclass(frozen=True, slots=True)
class CrackOptions:
    strategy: CrackStrategy = CrackStrategy.LLL
    enum_bound: int = DEFAULT_ENUM_BOUND
    max_enum_candidates: int = DEFAULT_MAX_ENUM_CANDIDATES
    max_crack_len: int | None = None


def normalize_strategy(strategy: CrackStrategy | str) -> CrackStrategy:
    if isinstance(strategy, CrackStrategy):
        return strategy
    if isinstance(strategy, str):
        try:
            return CrackStrategy(strategy)
        except ValueError:
            pass
    raise ValueError("strategy must be 'lll' or 'enumerate'")


def native_strategy(strategy: CrackStrategy) -> int:
    if strategy == CrackStrategy.LLL:
        return int(_NativeStrategy.LLL)
    return int(_NativeStrategy.ENUMERATE)


def check_uint(name: str, value: int, bits: int) -> int:
    if not isinstance(value, int):
        raise TypeError(f"{name} must be an int")
    if value < 0:
        raise ValueError(f"{name} must be non-negative")
    max_value = (1 << bits) - 1
    if value > max_value:
        raise OverflowError(f"{name} must fit in uint{bits}")
    return value


def normalize_options(options: CrackOptions | None) -> CrackOptions:
    if options is None:
        return CrackOptions()
    if not isinstance(options, CrackOptions):
        raise TypeError("options must be a CrackOptions instance")
    return CrackOptions(
        strategy=normalize_strategy(options.strategy),
        enum_bound=check_uint("enum_bound", options.enum_bound, 32),
        max_enum_candidates=check_uint("max_enum_candidates", options.max_enum_candidates, 64),
        max_crack_len=(
            None if options.max_crack_len is None
            else check_uint("max_crack_len", options.max_crack_len, 32)
        ),
    )
