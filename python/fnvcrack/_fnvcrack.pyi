from collections.abc import Callable
from typing import TypeAlias


_Buffer: TypeAlias = bytes | bytearray | memoryview

__version__: str
"""Package version reported by the native extension."""


class CrackError(Exception):
    """Raised when the native cracking layer reports a binding error."""


class NativeContext:
    """Native FNV-1a cracking context.

    :param offset_basis: Initial FNV hash state, or ``None`` for the default.
    :param prime: FNV prime multiplier, or ``None`` for the default.
    :param bit_length: Number of hash bits to use, or ``None`` for the default.
    :param prefix: Known bytes at the beginning of the input.
    :param suffix: Known bytes at the end of the input.
    :param valid_chars: Bytes allowed in cracked output. ``None`` allows all bytes.
    """

    def __init__(
        self,
        *,
        offset_basis: int | None = ...,
        prime: int | None = ...,
        bit_length: int | None = ...,
        prefix: _Buffer | None = ...,
        suffix: _Buffer | None = ...,
        valid_chars: _Buffer | None = ...,
    ) -> None: ...

    @property
    def offset_basis(self) -> int:
        """Initial FNV hash state used by this context."""
        ...

    @property
    def prime(self) -> int:
        """FNV prime multiplier used by this context."""
        ...

    @property
    def bit_length(self) -> int:
        """Number of hash bits used by this context."""
        ...

    @property
    def prefix(self) -> bytes:
        """Known bytes prepended to candidate inputs."""
        ...

    @property
    def suffix(self) -> bytes:
        """Known bytes appended to candidate inputs."""
        ...

    @property
    def valid_chars(self) -> bytes:
        """Bytes allowed in cracked output."""
        ...

    def crack(
        self,
        target: int,
        crack_len: int,
        enum_bound: int,
        max_enum_candidates: int,
        incremental: bool,
        callback: Callable[[bytes], bool] | None = ...,
        threads: int = ...,
    ) -> tuple[int, bytes | None]:
        """Try to crack ``target`` using fully normalized native arguments.

        :param target: Hash value to crack.
        :param crack_len: Unknown byte length to crack.
        :param enum_bound: Search radius around the lattice solution.
        :param max_enum_candidates: Maximum enumeration candidates. ``0`` means unlimited.
        :param incremental: Search all unknown lengths from 1 through ``crack_len``.
        :param callback: Receives verified candidates and returns whether to accept them.
        :param threads: Maximum worker count for one shared lattice enumeration per exact search length.
        Short searches can be slower. Callback calls are fully serialized and candidate order is unspecified.
        :returns: A ``(status, value)`` pair.
        """
        ...

    def __repr__(self) -> str:
        """Return a string representation of this native context."""
        ...

    def __eq__(self, other: object) -> bool:
        """Return whether another native context has the same configuration."""
        ...
