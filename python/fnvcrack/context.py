from . import _fnvcrack
from .options import CrackOptions, check_uint, native_strategy, normalize_options
from .result import CrackResult


FNV64_OFFSET_BASIS = 0xcbf29ce484222325
FNV64_PRIME = 0x100000001b3


class CrackContext:
    def __init__(
        self,
        *,
        offset_basis: int = FNV64_OFFSET_BASIS,
        prime: int = FNV64_PRIME,
        bit_length: int = 64,
        valid_chars: bytes | None = None,
        brute_chars: bytes | None = None,
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
            brute_chars=brute_chars,
        )

    @property
    def offset_basis(self) -> int:
        return self._native.offset_basis

    @property
    def prime(self) -> int:
        return self._native.prime

    @property
    def bit_length(self) -> int:
        return self._native.bit_length

    @property
    def prefix(self) -> bytes:
        return self._native.prefix

    @property
    def suffix(self) -> bytes:
        return self._native.suffix

    @property
    def valid_chars(self) -> bytes:
        return self._native.valid_chars

    @property
    def brute_chars(self) -> bytes:
        return self._native.brute_chars

    def crack(
        self,
        target: int,
        max_len: int,
        options: CrackOptions | None = None,
    ) -> CrackResult:
        if not isinstance(target, int):
            raise TypeError("target must be an int")

        target = check_uint("target", target, self.bit_length)
        max_len = check_uint("max_len", max_len, 32)
        if max_len + len(self.prefix) + len(self.suffix) > (1 << 32) - 1:
            raise OverflowError("max_len plus prefix and suffix lengths must fit in uint32")
        normalized = normalize_options(options)
        max_crack_len = (
            self.bit_length // 8
            if normalized.max_crack_len is None
            else normalized.max_crack_len
        )

        status, value = self._native.crack(
            target,
            max_len,
            max_crack_len,
            native_strategy(normalized.strategy),
            normalized.enum_bound,
            normalized.max_enum_candidates,
        )
        return CrackResult(status=status, value=value)

    def __repr__(self) -> str:
        return (
            f"{type(self).__name__}("
            f"prime={self.prime!r}, "
            f"offset_basis={self.offset_basis!r}, "
            f"bit_length={self.bit_length!r}, "
            f"prefix={self.prefix!r}, "
            f"suffix={self.suffix!r}, "
            f"valid_chars={self.valid_chars!r}, "
            f"brute_chars={self.brute_chars!r})"
        )

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, CrackContext):
            return NotImplemented
        return self._native == other._native
