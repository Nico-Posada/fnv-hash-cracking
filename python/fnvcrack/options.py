from dataclasses import dataclass
from enum import IntEnum, StrEnum


DEFAULT_ENUM_BOUND = 4
"""Default local enumeration radius for the enumerate strategy."""

DEFAULT_MAX_ENUM_CANDIDATES = 0
"""Default candidate limit for enumeration. ``0`` means unlimited."""


class CrackStrategy(StrEnum):
    """Available cracking strategies."""

    LLL = "lll"
    """Use the LLL-based lattice strategy."""

    ENUMERATE = "enumerate"
    """Use bounded enumeration around the lattice solution."""


class _NativeStrategy(IntEnum):
    LLL = 0
    ENUMERATE = 1


@dataclass(frozen=True, slots=True)
class CrackOptions:
    """Options passed to :meth:`fnvcrack.CrackContext.crack`.

    :ivar strategy: Cracking strategy to use.
    :ivar enum_bound: Search radius for the enumerate strategy.
    :ivar max_enum_candidates: Maximum enumeration candidates. ``0`` means unlimited.
    :ivar max_crack_len: Maximum bytes solved without brute force. ``None`` uses
        ``bit_length // 8``.
    """

    strategy: CrackStrategy = CrackStrategy.LLL
    enum_bound: int = DEFAULT_ENUM_BOUND
    max_enum_candidates: int = DEFAULT_MAX_ENUM_CANDIDATES
    max_crack_len: int | None = None


def normalize_strategy(strategy: CrackStrategy | str) -> CrackStrategy:
    """Convert a strategy string or enum value to :class:`CrackStrategy`.

    :param strategy: Strategy value or string name.
    :returns: Normalized strategy enum value.
    :raises ValueError: If the strategy is unknown.
    """
    if isinstance(strategy, CrackStrategy):
        return strategy
    if isinstance(strategy, str):
        try:
            return CrackStrategy(strategy)
        except ValueError:
            pass
    raise ValueError("strategy must be 'lll' or 'enumerate'")


def native_strategy(strategy: CrackStrategy) -> int:
    """Return the native integer value for a crack strategy.

    :param strategy: Strategy enum value.
    :returns: Integer strategy id expected by the native extension.
    """
    if strategy == CrackStrategy.LLL:
        return int(_NativeStrategy.LLL)
    return int(_NativeStrategy.ENUMERATE)


def check_uint(name: str, value: int, bits: int) -> int:
    """Validate that ``value`` fits an unsigned integer width.

    :param name: Argument name used in error messages.
    :param value: Value to validate.
    :param bits: Unsigned integer width in bits.
    :returns: The validated value.
    :raises TypeError: If ``value`` is not an exact ``int``.
    :raises ValueError: If ``value`` is negative.
    :raises OverflowError: If ``value`` is too large.
    """
    if type(value) is not int:
        raise TypeError(f"{name} must be an int")
    if value < 0:
        raise ValueError(f"{name} must be non-negative")
    if value.bit_length() > bits:
        raise OverflowError(f"{name} must fit in uint{bits}")
    return value


def normalize_options(options: CrackOptions | None) -> CrackOptions:
    """Return validated crack options with defaults applied.

    :param options: Options to validate, or ``None`` for defaults.
    :returns: A normalized :class:`CrackOptions` instance.
    :raises TypeError: If ``options`` is not a :class:`CrackOptions` instance.
    :raises ValueError: If an option has an invalid value.
    :raises OverflowError: If an option does not fit native limits.
    """
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
