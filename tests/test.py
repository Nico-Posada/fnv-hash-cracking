import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent.parent / "ext"))
import unittest
from fnvcrack import CrackContext


class TestCrackContextConstructor(unittest.TestCase):
    def setUp(self):
        self.default_params = {
            'offset_basis': 0xcbf29ce484222325,
            'prime': 0x00000100000001b3,
            'bit_length': 64,
            'prefix': b'test',
            'suffix': b'suffix',
            'valid_chars': b'abcdefghijklmnopqrstuvwxyz',
            'brute_chars': b'abcdefghijklmnopqrstuvwxyz'
        }
    
    def test_constructor_with_all_valid_parameters(self):
        ctx = CrackContext(**self.default_params)
        self.assertIsInstance(ctx, CrackContext)
    
    def test_constructor_with_only_bit_length(self):
        ctx = CrackContext(prime=0, offset_basis=0, bit_length=32)
        self.assertIsInstance(ctx, CrackContext)
    
    def test_totally_empty_constructor(self):
        CrackContext()
    
    def test_bit_length_zero_raises_value_error(self):
        with self.assertRaises(ValueError) as cm:
            CrackContext(bit_length=0)
        self.assertIn("bit_length should be a non-zero value", str(cm.exception))
    
    def test_bit_length_negative_raises_value_error(self):
        with self.assertRaises(ValueError):
            CrackContext(bit_length=-1)
    
    def test_bit_length_string_raises_type_error(self):
        with self.assertRaises(TypeError):
            CrackContext(bit_length="64")
    
    def test_bit_length_float_raises_type_error(self):
        with self.assertRaises(TypeError):
            CrackContext(bit_length=64.5)
    
    def test_bit_length_exceeds_uint32_max_raises_overflow_error(self):
        with self.assertRaises(OverflowError):
            CrackContext(bit_length=2**32)
    
    def test_bit_length_valid_values_create_context(self):
        for bit_len in [1, 8, 16, 32, 64, 128, 256]:
            ctx = CrackContext(prime=0, offset_basis=0, bit_length=bit_len)
            self.assertEqual(ctx.bit_length, bit_len)
    
    def test_offset_basis_none_uses_default_value(self):
        ctx = CrackContext(bit_length=64)
        self.assertEqual(ctx.offset_basis, 0xcbf29ce484222325)
    
    def test_offset_basis_string_raises_type_error(self):
        with self.assertRaises(TypeError):
            CrackContext(bit_length=64, offset_basis="invalid")
    
    def test_offset_basis_float_raises_type_error(self):
        with self.assertRaises(TypeError):
            CrackContext(bit_length=64, offset_basis=3.14)
    
    def test_offset_basis_exceeds_bit_length_raises_type_error(self):
        with self.assertRaises(TypeError):
            CrackContext(bit_length=8, offset_basis=0x1FF)
    
    def test_offset_basis_valid_values_accepted(self):
        ctx = CrackContext(bit_length=64, offset_basis=0x123456789ABCDEF0)
        self.assertEqual(ctx.offset_basis, 0x123456789ABCDEF0)
    
    def test_prime_none_uses_default_value(self):
        ctx = CrackContext(bit_length=64)
        self.assertEqual(ctx.prime, 0x00000100000001b3)
    
    def test_prime_string_raises_type_error(self):
        with self.assertRaises(TypeError):
            CrackContext(bit_length=64, prime="invalid")
    
    def test_prime_float_raises_type_error(self):
        with self.assertRaises(TypeError):
            CrackContext(bit_length=64, prime=2.5)
    
    def test_prime_exceeds_bit_length_raises_type_error(self):
        with self.assertRaises(TypeError):
            CrackContext(bit_length=8, prime=0x1FF)
    
    def test_prime_valid_values_accepted(self):
        ctx = CrackContext(bit_length=64, prime=0x1234567890ABCDEF)
        self.assertEqual(ctx.prime, 0x1234567890ABCDEF)
    
    def test_prefix_none_creates_empty_buffer(self):
        ctx = CrackContext(bit_length=64, prefix=None)
        self.assertEqual(ctx.prefix, b'')
    
    def test_prefix_string_raises_type_error(self):
        with self.assertRaisesRegex(TypeError, r"must be a buffer object, not a str"):
            CrackContext(bit_length=64, prefix="string")
    
    def test_prefix_bytes_accepted(self):
        ctx = CrackContext(bit_length=64, prefix=b'test')
        self.assertEqual(ctx.prefix, b'test')
    
    def test_prefix_bytearray_accepted(self):
        ctx = CrackContext(bit_length=64, prefix=bytearray(b'test'))
        self.assertEqual(ctx.prefix, b'test')
    
    def test_prefix_memoryview_accepted(self):
        ctx = CrackContext(bit_length=64, prefix=memoryview(b'test'))
        self.assertEqual(ctx.prefix, b'test')
    
    def test_nonstandard_buffer_works(self):
        import array
        multidim = array.array('i', [1, 2, 3, 4])
        CrackContext(bit_length=64, prefix=multidim)
    
    def test_suffix_none_creates_empty_buffer(self):
        ctx = CrackContext(bit_length=64, suffix=None)
        self.assertEqual(ctx.suffix, b'')
    
    def test_suffix_string_raises_type_error(self):
        with self.assertRaisesRegex(TypeError, r"must be a buffer object, not a str"):
            CrackContext(bit_length=64, suffix="string")
    
    def test_suffix_bytes_accepted(self):
        ctx = CrackContext(bit_length=64, suffix=b'test')
        self.assertEqual(ctx.suffix, b'test')
    
    def test_suffix_bytearray_accepted(self):
        ctx = CrackContext(bit_length=64, suffix=bytearray(b'test'))
        self.assertEqual(ctx.suffix, b'test')
    
    def test_valid_chars_none_creates_full_buffer(self):
        ctx = CrackContext(bit_length=64, valid_chars=None)
        self.assertEqual(ctx.valid_chars, bytes(range(256)))
    
    def test_valid_chars_string_raises_type_error(self):
        with self.assertRaisesRegex(TypeError, r"must be a buffer object, not a str"):
            CrackContext(bit_length=64, valid_chars="string")
    
    def test_valid_chars_bytes_accepted(self):
        ctx = CrackContext(bit_length=64, valid_chars=b'abc')
        self.assertEqual(ctx.valid_chars, b'abc')
    
    def test_brute_chars_none_creates_empty_buffer(self):
        ctx = CrackContext(bit_length=64, brute_chars=None)
        self.assertEqual(ctx.brute_chars, b'')
    
    def test_brute_chars_string_raises_type_error(self):
        with self.assertRaisesRegex(TypeError, r"must be a buffer object, not a str") as cm:
            CrackContext(bit_length=64, brute_chars="string")
    
    def test_brute_chars_bytes_accepted(self):
        ctx = CrackContext(bit_length=64, brute_chars=b'xyz')
        self.assertEqual(ctx.brute_chars, b'xyz')
    
    def test_invalid_keyword_argument_raises_type_error(self):
        with self.assertRaises(TypeError):
            CrackContext(bit_length=64, invalid_param="test")
    
    def test_constructor_with_mixed_valid_and_none_parameters(self):
        ctx = CrackContext(
            bit_length=48,
            offset_basis=0x12345678,
            prime=None,
            prefix=b'pre',
            suffix=None,
            valid_chars=b'abc',
            brute_chars=None
        )
        self.assertEqual(ctx.bit_length, 48)
        self.assertEqual(ctx.offset_basis, 0x12345678)
        self.assertEqual(ctx.prime, 0x00000100000001b3)
        self.assertEqual(ctx.prefix, b'pre')
        self.assertEqual(ctx.suffix, b'')
        self.assertEqual(ctx.valid_chars, b'abc')
        self.assertEqual(ctx.brute_chars, b'')

class TestKnownWorkingCracks(unittest.TestCase):
    @staticmethod
    def _fnv(data, prime, offset, bits):
        hsh = offset
        for c in data:
            hsh ^= c
            hsh *= prime
            hsh %= 2**bits
        return hsh

    def _do_test(self, pt_list, prime, offset_basis, bits):
        ctx = CrackContext(
            prime=prime,
            offset_basis=offset_basis,
            bit_length=bits
        )

        for pt in pt_list:
            hashed = self._fnv(pt, prime, offset_basis, bits)
            err, result = ctx.crack(hashed, bits // 8, bits // 8)
            self.assertGreaterEqual(err, -1)
            if err == -1 or result is None:
                raise RuntimeError(f"Failed {hashed:#x} => {pt!r} ({err}, {result})")

            if result != pt and (fake := self._fnv(result, prime, offset_basis, bits)) != hashed:
                raise RuntimeError(f"Got invalid result for {pt!r} ({hashed:#x} vs {fake:#x})")

    def test_64_bit_cracks(self):
        plaintext = (
            b"abcdefg",
            b"bbbb",
            b"\x90\x90\x90\x90\x90\x90",
            b"qqq111",
            b"\x00\x01\x02\x03\x04\x05\x06\x07",
            b"01234567",
            b"test",
            b"xyz",
            b"\xFF\xFE\xFD\xFC\xFF\xFE",
            b"crack",
            b"hello",
            b"\x00\x00\x00",
            b"python",
            b"a" * 8
        )

        PRIME = 0x00000100000001b3
        OFFSET_BASIS = 0xcbf29ce484222325
        BITS = 64
        self._do_test(plaintext, PRIME, OFFSET_BASIS, BITS)

    def test_128_bit_cracks(self):
        plaintext = (
            b"hello_world",
            b"test_case",
            b"\x01\x02\x03\x04\x05\x06\x07\x08",
            b"fnv_hash_128",
            b"\x00" * 10,
            b"python_test",
            b"cracking_fnv",
            # b"\xFF\x00"*8,
            b"boundary_test",
            # b"a" * 16,
            b"short",
            # b"\x80\x81\x82\x83",
            b"medium_length",
            b"0123456789ABCDEF"
        )

        PRIME = 0x0000000001000000000000000000013b
        OFFSET_BASIS = 0x6c62272e07bb014262b821756295c58d
        BITS = 128
        self._do_test(plaintext, PRIME, OFFSET_BASIS, BITS)

    def test_256_bit_cracks(self):
        plaintext = (
            b"extended_test_string_256",
            b"hash_cracking_algorithm",
            b"\x10\x20\x30\x40\x50\x60\x70",
            b"comprehensive_validation",
            b"\x00" * 20,
            b"fnv_256_bit_implementation",
            b"security_testing_mode",
            # b"\xAA\xBB\xCC\xDD" * 4,
            b"performance_benchmark",
            b"b" * 30,
            b"mini",
            # b"\x77\x88\x99\xAA\xBB",
            b"standard_length_test",
            b"0123456789ABCDEF" * 2
        )

        PRIME = 0x0000000000000000000001000000000000000000000000000000000000000163
        OFFSET_BASIS = 0xdd268dbcaac550362d98c384c4e576ccc8b1536847b6bbb31023b4c8caee0535
        BITS = 256
        self._do_test(plaintext, PRIME, OFFSET_BASIS, BITS)

    def test_512_bit_cracks(self):
        plaintext = (
            b"maximum_length_test_string_for_512_bit_fnv_hash_algorithm",
            b"cryptographic_hash_function_validation_and_testing_suite",
            # b"\x11\x22\x33\x44\x55\x66\x77\x88\x99\xAA\xBB\xCC\xDD\xEE\xFF\x00",
            # b"ultra_comprehensive_boundary_condition_testing_framework",
            b"\x00" * 64,
            # b"fnv_512_bit_hash_implementation_security_analysis_tool",
            b"advanced_cryptanalysis_and_reverse_engineering_testbed",
            # b"\xDE\xAD\xBE\xEF" * 8,
            b"performance_stress_testing_and_optimization_validation",
            # b"c" * 64,
            b"tiny",
            # b"\xF0\xF1\xF2\xF3\xF4\xF5",
            b"moderate_length_string_test",
            # b"0123456789ABCDEF" * 4
        )

        PRIME = 0x0000000000000000_0000000000000000_0000000001000000_0000000000000000_0000000000000000_0000000000000000_0000000000000000_0000000000000157
        OFFSET_BASIS = 0xb86db0b1171f4416_dca1e50f309990ac_ac87d059c9000000_0000000000000d21_e948f68a34c192f6_2ea79bc942dbe7ce_182036415f56e34b_ac982aac4afe9fd9
        BITS = 512
        self._do_test(plaintext, PRIME, OFFSET_BASIS, BITS)

if __name__ == '__main__':
    unittest.main()