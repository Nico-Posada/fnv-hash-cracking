from __future__ import annotations
from hashlib import sha256
import ctypes
import subprocess
import sys, os

assert sys.platform != 'win32', "This will not work with windows, try using WSL2."

__all__ = ["CrackLib", "presets"]

class _CrackLibWrapper:
    def __init__(self, lib):
        self._lib = lib
    
    def init(self, valid_chars: str | bytes | bytearray, bruting_chars: str | bytes | bytearray) -> None:
        e = lambda s: bytes(s) if isinstance(s, bytes | bytearray) else s.encode("utf-8")
        self._lib.init(
            e(valid_chars),
            e(bruting_chars)
        )
    
    def crack_hash(self, hash: int, max_brute: int) -> None | bytes:
        buffer_size = 256
        result_buffer = ctypes.create_string_buffer(buffer_size)
        if self._lib.crack_hash(
            ctypes.c_uint64(hash),
            result_buffer,
            ctypes.c_size_t(buffer_size),
            ctypes.c_uint32(max_brute)
        ):
            return result_buffer.value

class CrackLib:
    def __init__(self, *,
                 lib_src="./base_lib",
                 src="../../src",
                 libcache_path="./.libcache"):
        self._local_libcache: dict[tuple[int, int, int], _CrackLibWrapper] = {}
        self._libcache_path = libcache_path
        self._source_location = src
        self._lib_src = lib_src

    def load_crack_lib(self, offset: int, prime: int, bits=64):
        # check to see if we have loaded the library at all during this runtime
        if (lib := self._local_libcache.get((offset, prime, bits))) is not None:
            return lib
        
        def _init_and_cache_lib(lib):
            nonlocal self, offset, prime, bits
            lib.init.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
            lib.init.restype  = None
            lib.crack_hash.argtypes = [ctypes.c_uint64, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_uint32]
            lib.crack_hash.restype = ctypes.c_bool
            result = _CrackLibWrapper(lib)
            self._local_libcache[offset, prime, bits] = result
            return result
    
        os.makedirs(self._libcache_path, exist_ok=True)

        # make unique library name
        library_hash = sha256(f"{offset}{prime}{bits}".encode()).hexdigest()[:16]
        library_name = f"__lib_{library_hash}.so"
        library_path = os.path.join(self._libcache_path, library_name)
        source_path = os.path.join(self._lib_src, "cracklib.cpp")

        # if it exists, no need to recompile it
        if os.path.exists(library_path):
            print("[+] Using cached library", library_name)
            return _init_and_cache_lib(ctypes.CDLL(library_path))
        
        print("[+] Building library...")
        args = ["g++", "-x", "c++", "-march=native", "-O3", "-std=c++20",
                "-shared", "-fPIC",
                "-D", f"_OFFSET={offset:#x}",
                "-D", f"_PRIME={prime:#x}",
                "-D", f"_BITS={bits}",
                "-o", library_path, source_path,
                "-lfplll", "-lgmp", f"-I{self._source_location}"]

        if ret_code := subprocess.call(args, stdout=subprocess.DEVNULL, text=True):
            raise RuntimeError(f"Failed to build library with return code {ret_code}")
        
        print("[+] Finished building and cached the library")
        return _init_and_cache_lib(ctypes.CDLL(library_path))

class presets:
    printable = " !\"#$%&'()*+,-./0123456789:<=>?@[]^_`abcdefghijklmnopqrstuvwxyz{|}~"
    alpha = "abcdefghijklmnopqrstuvwxyz"
    alphanum = "abcdefghijklmnopqrstuvwxyz0123456789"
    hex = "0123456789abcdef"
    ident = "abcdefghijklmnopqrstuvwxyz0123456789_"

    PRIME_1B3 = 0x100000001b3
    PRIME_233 = 0x10000000233
    OFFSET_DEFAULT = 0xcbf29ce484222325

# super basic example, can use for easy multithreading of a lot of hashes or something
if __name__ == "__main__":
    import time
    crack = CrackLib()

    # i'm using the default offset and prime, but you can change them here
    mod = crack.load_crack_lib(presets.OFFSET_DEFAULT, presets.PRIME_1B3)

    # just some random hash I had lying around
    HASH_TO_CRACK = 0x43335052F5BFEBF2

    mod.init(valid_chars=presets.ident, bruting_chars=presets.ident)
    start = time.time()
    
    print(mod.crack_hash(HASH_TO_CRACK, 10)) # => b'mediumtile'
    end = time.time()
    print(f"Found hash in {end - start:.4f}s")