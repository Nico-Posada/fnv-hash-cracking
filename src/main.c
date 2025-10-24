#include <stdio.h>
#include <time.h>

#include <flint/fmpz.h>

#include "context.h"
#include "inverse.h"
#include "brute_gen.h"
#include "fnv.h"
#include "crack.h"

inline uint64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

void run_64_bit_crack() {
    CREATE_CONTEXT(ctx);
    if (!init_crack_ctx(
        ctx,
        // offset basis
        0xCBF29CE484222325,
        // prime
        0x100000001B3,
        // max value (2^x), 64 bits should be the most common one you'll see
        64,
        // brute chars
        "abcdefghijklmnopqrstuvwxyz",
        // valid chars
        "abcdefghijklmnopqrstuvwxyz",
        // known prefix
        "qqq",
        // known suffix
        NULL
    )) {
        printf("Failed to initialize ctx in run_64_bit_crack\n");
        return;
    }
    // print_context(ctx);
    uint64_t hashed = fnv_u64("qqqzzcdefghij", 0xCBF29CE484222325, 0x100000001B3, 64);
    printf("Hashed: %lx\n", hashed);

    char_buffer buf = {NULL, 0};
    const uint64_t start = get_time_ns();
    CrackResult ret = crack_u64_with_len(ctx, hashed, &buf, 13, 2);
    const uint64_t end = get_time_ns();
    printf("result: %s\nstr: %s\ntime: %.4lf\n", result_as_str(ret), buf.data ? buf.data : "(failed)", (end - start) / 1000000000.0);
    clear_char_buffer(&buf);
    destroy_crack_ctx(ctx);
}

// Solver for https://github.com/quasar098/ictf-archive/blob/master/round-56/FNV-1a-chal.py
// Cracking an FNV-1a hash that uses mod 2**320
void run_320_bit_crack() {
    fmpz_t offset_basis, prime;
    fmpz_init(offset_basis); fmpz_init(prime);
    fmpz_set_str(offset_basis, "86478568332086667988955226522744024433416290808708427009300709942571393030379", 10);
    fmpz_set_str(prime, "58212954222403626346155684772977216669103315464820228336508867619615003388891", 10);

    fmpz_t hashed; fmpz_init(hashed);
    fmpz_set_str(hashed, "923278209713176653012807450506579337424686596606979155232335733448961331039798473007051981204278", 10);

    CREATE_CONTEXT(ctx);
    init_crack_fmpz_ctx(
        ctx,
        offset_basis,
        prime,
        320,
        // brute chars
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!\"#$%&\'()*+,-./:;<=>?@[\\]^_`{|}~ ",
        // valid chars
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!\"#$%&\'()*+,-./:;<=>?@[\\]^_`{|}~ ",
        // known prefix, known suffix (challenge uses bytes_to_long so the string will be backwards)
        "}", "{ftci"
    );

    // set up the char buffer and try cracking
    char_buffer buf = {NULL, 0};
    const uint64_t start = get_time_ns();
    CrackResult ret = crack_fmpz_with_len(ctx, hashed, &buf, 37, 0);
    const uint64_t end = get_time_ns();

    // print results (output string will be reversed because it used bytes_to_long on it in the challenge)
    printf("result: %s\nstr: %s\ntime: %.4lf\n", result_as_str(ret), buf.data ? buf.data : "(failed)", (end - start) / 1000000000.0);

    // clean up
    fmpz_clear(offset_basis);
    fmpz_clear(prime);
    fmpz_clear(hashed);
    clear_char_buffer(&buf);
    destroy_crack_ctx(ctx);
}

void run_64_bit_unk() {
    CREATE_CONTEXT(ctx);
    if (!init_crack_ctx(ctx, 0xCBF29CE484222325, 0x100000001B3, 64, "abcdefghijklmnopqrstuvwxyz", "abcdefghijklmnopqrstuvwxyz", "qqq", NULL)) {
        printf("Failed to initialize ctx in run_64_bit_crack\n");
        return;
    }
    // print_context(ctx);
    uint64_t hashed = fnv_u64("qqqzzcdefghij", 0xCBF29CE484222325, 0x100000001B3, 64);
    printf("Hashed: %lx\n", hashed);

    char_buffer buf = {NULL, 0};
    const uint64_t start = get_time_ns();
    CrackResult ret = crack_u64(ctx, hashed, &buf, 10, 8);
    const uint64_t end = get_time_ns();
    printf("result: %s\nstr: %s\ntime: %.4lf\n", result_as_str(ret), buf.data ? buf.data : "(failed)", (end - start) / 1000000000.0);
    clear_char_buffer(&buf);
    destroy_crack_ctx(ctx);
}

int main()
{
    run_64_bit_crack();
    run_320_bit_crack();
    run_64_bit_unk();

    return 1;
}