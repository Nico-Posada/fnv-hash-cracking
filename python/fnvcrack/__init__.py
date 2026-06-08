"""Python bindings for the FNV-1a cracking library."""

from ._fnvcrack import CrackError, __version__
from .context import FNV64_OFFSET_BASIS, FNV64_PRIME, CrackContext
from .options import CrackOptions, CrackStrategy, DEFAULT_ENUM_BOUND
from .result import CrackResult, CrackStatus


STRATEGY_LLL = CrackStrategy.LLL
"""Alias for :attr:`CrackStrategy.LLL`."""

STRATEGY_ENUMERATE = CrackStrategy.ENUMERATE
"""Alias for :attr:`CrackStrategy.ENUMERATE`."""


__all__ = [
    "CrackContext",
    "CrackError",
    "CrackOptions",
    "CrackResult",
    "CrackStatus",
    "CrackStrategy",
    "DEFAULT_ENUM_BOUND",
    "FNV64_OFFSET_BASIS",
    "FNV64_PRIME",
    "STRATEGY_ENUMERATE",
    "STRATEGY_LLL",
    "__version__",
]
