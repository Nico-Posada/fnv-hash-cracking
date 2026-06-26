from . import _fnvcrack
from .result import CrackResult


FNV64_OFFSET_BASIS = 0xcbf29ce484222325
"""Default 64-bit FNV-1a offset basis."""

FNV64_PRIME = 0x100000001b3
"""Default 64-bit FNV-1a prime."""

DEFAULT_ENUM_BOUND = 4
"""Default local enumeration radius."""

DEFAULT_MAX_ENUM_CANDIDATES = 0
"""Default candidate limit for enumeration. ``0`` means unlimited."""


class CrackContext:
    """Configuration for cracking FNV-1a hashes.

    :param offset_basis: Initial FNV hash state.
    :param prime: FNV prime multiplier. Must be odd.
    :param bit_length: Number of hash bits to use.
    :param valid_chars: Bytes allowed in cracked output. ``None`` allows all bytes.
    :param prefix: Known bytes at the beginning of the input.
    :param suffix: Known bytes at the end of the input.
    """

    def __init__(
        self,
        *,
        offset_basis: int = FNV64_OFFSET_BASIS,
        prime: int = FNV64_PRIME,
        bit_length: int = 64,
        valid_chars: bytes | None = None,
        prefix: bytes = b"",
        suffix: bytes = b"",
    ) -> None:
        self._native = _fnvcrack.NativeContext(
            offset_basis=offset_basis,
            prime=prime,
            bit_length=bit_length,
            prefix=prefix,
            suffix=suffix,
            valid_chars=valid_chars,
        )

    @property
    def offset_basis(self) -> int:
        """Initial FNV hash state used by this context."""
        return self._native.offset_basis

    @property
    def prime(self) -> int:
        """FNV prime multiplier used by this context."""
        return self._native.prime

    @property
    def bit_length(self) -> int:
        """Number of hash bits used by this context."""
        return self._native.bit_length

    @property
    def prefix(self) -> bytes:
        """Known bytes prepended to candidate inputs."""
        return self._native.prefix

    @property
    def suffix(self) -> bytes:
        """Known bytes appended to candidate inputs."""
        return self._native.suffix

    @property
    def valid_chars(self) -> bytes:
        """Bytes allowed in cracked output."""
        return self._native.valid_chars

    def crack(
        self,
        target: int,
        crack_len: int,
        enum_bound: int = DEFAULT_ENUM_BOUND,
        max_enum_candidates: int = DEFAULT_MAX_ENUM_CANDIDATES,
        incremental: bool = False,
    ) -> CrackResult:
        """Try to find an input whose FNV-1a hash matches ``target``.

        :param target: Hash value to crack.
        :param crack_len: Unknown byte length to crack.
        :param enum_bound: Search radius around the lattice solution.
        :param max_enum_candidates: Maximum enumeration candidates. ``0`` means unlimited.
        :param incremental: Search all unknown lengths from 1 through ``crack_len``.
        :returns: The crack status and value, if one was found.
        :raises TypeError: If arguments have invalid types.
        :raises ValueError: If integer arguments are negative.
        :raises OverflowError: If integer arguments do not fit native limits.
        """
        status, value = self._native.crack(
            target,
            crack_len,
            enum_bound,
            max_enum_candidates,
            incremental,
        )
        return CrackResult(status=status, value=value)

    def __repr__(self) -> str:
        return repr(self._native)

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, CrackContext):
            return NotImplemented
        return self._native == other._native
