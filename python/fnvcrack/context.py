from collections.abc import Callable, Iterable
from multiprocessing import Pool

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


class CrackContext(_fnvcrack.NativeContext):
    """Configuration for cracking FNV-1a hashes.

    :param offset_basis: Initial FNV hash state.
    :param prime: FNV prime multiplier. Must be odd.
    :param bit_length: Number of hash bits to use.
    :param prefix: Known bytes at the beginning of the input.
    :param suffix: Known bytes at the end of the input.
    :param valid_chars: Bytes allowed in cracked output. ``None`` allows all bytes.
    """

    __slots__ = ()

    def __reduce__(self) -> tuple[type, tuple[int, int, int, bytes, bytes, bytes]]:
        return (
            type(self),
            (
                self.offset_basis,
                self.prime,
                self.bit_length,
                self.prefix,
                self.suffix,
                self.valid_chars,
            ),
        )

    def crack(
        self,
        target: int,
        crack_len: int,
        enum_bound: int = DEFAULT_ENUM_BOUND,
        max_enum_candidates: int = DEFAULT_MAX_ENUM_CANDIDATES,
        incremental: bool = False,
        callback: Callable[[bytes], bool] | None = None,
    ) -> CrackResult:
        """Try to find an input whose FNV-1a hash matches ``target``.

        :param target: Hash value to crack.
        :param crack_len: Unknown byte length to crack.
        :param enum_bound: Search radius around the lattice solution.
        :param max_enum_candidates: Maximum enumeration candidates. ``0`` means unlimited.
        :param incremental: Search all unknown lengths from 1 through ``crack_len``.
        :param callback: Receives each verified candidate. A truthy return accepts it; falsey continues searching.
        :returns: The crack status and accepted value, if any. With ``callback=None``, the first verified match is
            accepted. If a callback rejects every candidate, the result is ``FAILED`` with no value.
        A custom ``SIGINT`` handler that returns normally produces
        ``CrackResult(status=CrackStatus.INTERRUPTED, value=None)``.
        :raises TypeError: If arguments have invalid types.
        :raises ValueError: If integer arguments are negative.
        :raises OverflowError: If integer arguments do not fit native limits.
        :raises KeyboardInterrupt: If ``SIGINT`` is received while Python's default handler is active.
        """
        status, value = super().crack(
            target,
            crack_len,
            enum_bound,
            max_enum_candidates,
            incremental,
            callback,
        )
        return CrackResult(status=status, value=value)

    def batch_crack(
        self,
        targets: Iterable[int],
        crack_len: int,
        enum_bound: int = DEFAULT_ENUM_BOUND,
        max_enum_candidates: int = DEFAULT_MAX_ENUM_CANDIDATES,
        incremental: bool = False,
        processes: int | None = None,
    ) -> list[bytes | None]:
        """Crack targets in parallel and return ordered values or ``None``.

        :param targets: Hash values to crack.
        :param crack_len: Unknown byte length to crack.
        :param enum_bound: Search radius around the lattice solution.
        :param max_enum_candidates: Maximum enumeration candidates. ``0`` means unlimited.
        :param incremental: Search all unknown lengths from 1 through ``crack_len``.
        :param processes: Worker process count. ``None`` uses the multiprocessing default.
        :returns: Ordered cracked values, with ``None`` for unsuccessful targets.
        Call from within ``if __name__ == "__main__"`` on spawn-based platforms.
        """
        with Pool(processes=processes) as pool:
            results = pool.starmap(
                self.crack,
                (
                    (
                        target,
                        crack_len,
                        enum_bound,
                        max_enum_candidates,
                        incremental,
                    )
                    for target in targets
                ),
            )
        return [result.value if result.ok else None for result in results]
