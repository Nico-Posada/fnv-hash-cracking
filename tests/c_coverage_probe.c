#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#include <flint/fmpz.h>

#include "context.h"
#include "crack.h"
#include "fnv.h"
#include "interrupt.h"
#include "inverse.h"

#define FNV64_OFFSET_BASIS 0xcbf29ce484222325ULL
#define FNV64_PRIME 0x100000001b3ULL

static char_buffer buf(const char* data) {
    return (char_buffer){data, strlen(data)};
}

static void clear_result(char_buffer* out) {
    clear_char_buffer(out);
    out->data = NULL;
    out->length = 0;
}

static size_t current_address_space(void) {
    FILE* fp = fopen("/proc/self/statm", "r");
    if (!fp) {
        return 0;
    }

    size_t pages = 0;
    if (fscanf(fp, "%zu", &pages) != 1) {
        pages = 0;
    }
    fclose(fp);
    return pages * (size_t)sysconf(_SC_PAGESIZE);
}

static bool limit_address_space(size_t extra, struct rlimit* old_limit) {
    if (getrlimit(RLIMIT_AS, old_limit) != 0) {
        return false;
    }

    const size_t current = current_address_space();
    if (current == 0) {
        return false;
    }

    struct rlimit limit = *old_limit;
    limit.rlim_cur = current + extra;
    if (limit.rlim_max != RLIM_INFINITY && limit.rlim_cur > limit.rlim_max) {
        return false;
    }
    return setrlimit(RLIMIT_AS, &limit) == 0;
}

static void check_interrupt_api(void) {
    assert(fnvcrack_restore_interrupt_handler() == 0);
    assert(fnvcrack_install_interrupt_handler() == 0);
    assert(fnvcrack_install_interrupt_handler() == 0);
    assert(fnvcrack_restore_interrupt_handler() == 0);
    assert(fnvcrack_restore_interrupt_handler() == 0);
    assert(fnvcrack_restore_interrupt_handler() == 0);
    fnvcrack_clear_interrupt();
    assert(!fnvcrack_interrupted());
}

static void check_inverse_api(void) {
    assert(inverse(0, 8) == 0);
    assert(((uint8_t)(3 * inverse(3, 8))) == 1);
    assert(FNV64_PRIME * inverse(FNV64_PRIME, 64) == 1);

    fmpz_t value, result;
    fmpz_init(value);
    fmpz_init(result);

    fmpz_set_ui(value, 3);
    inverse_fmpz(result, value, 128);
    assert(!fmpz_is_zero(result));

    fmpz_set_ui(value, 2);
    inverse_fmpz(result, value, 128);
    assert(fmpz_is_zero(result));

    fmpz_clear(result);
    fmpz_clear(value);
}

static void check_fnv_api(void) {
    assert(fnv_u64("abc", FNV64_OFFSET_BASIS, FNV64_PRIME, 64) ==
           fnv_u64_with_len(buf("abc"), FNV64_OFFSET_BASIS, FNV64_PRIME, 64));
    assert(fnv_u64("abc", FNV64_OFFSET_BASIS, FNV64_PRIME, 0) == 0);
    assert(fnv_u64("abc", FNV64_OFFSET_BASIS, FNV64_PRIME, 8) < 256);

    fmpz_t offset, prime, a, b;
    fmpz_init_set_ui(offset, FNV64_OFFSET_BASIS);
    fmpz_init_set_ui(prime, FNV64_PRIME);
    fmpz_init(a);
    fmpz_init(b);

    fnv_fmpz(a, "abc", offset, prime, 128);
    fnv_fmpz_with_len(b, buf("abc"), offset, prime, 128);
    assert(fmpz_equal(a, b));

    fmpz_clear(b);
    fmpz_clear(a);
    fmpz_clear(prime);
    fmpz_clear(offset);
}

static void check_context_api(void) {
    CREATE_CONTEXT(ctx);
    const char chars[] = "a\t\n\r\\\"\x01";
    const char prefix[] = "\t\n";
    const char suffix[] = "\r\\\"\x01";

    assert(!init_crack_ctx(ctx, FNV64_OFFSET_BASIS, FNV64_PRIME, 0, chars, prefix, suffix));
    assert(!init_crack_ctx(ctx, FNV64_OFFSET_BASIS, FNV64_PRIME, 65, chars, prefix, suffix));
    assert(init_crack_ctx(ctx, FNV64_OFFSET_BASIS, FNV64_PRIME, 64, chars, prefix, suffix));
    assert(set_prefix(ctx, *get_prefix(ctx)));
    assert(set_suffix(ctx, *get_suffix(ctx)));
    destroy_crack_ctx(ctx);

    fmpz_t offset, prime;
    fmpz_init_set_ui(offset, FNV64_OFFSET_BASIS);
    fmpz_init_set_ui(prime, FNV64_PRIME);

    CREATE_CONTEXT(fctx);
    assert(!init_crack_fmpz_ctx(fctx, offset, prime, 0, chars, prefix, suffix));
    assert(init_crack_fmpz_ctx(fctx, offset, prime, 128, chars, prefix, suffix));
    destroy_crack_ctx(fctx);

    fmpz_clear(prime);
    fmpz_clear(offset);
}

static void check_crack_api(void) {
    char_buffer out = {NULL, 0};

    CREATE_CONTEXT(empty);
    assert(crack_u64(empty, 0, &out, 1) == CONTEXT_UNINITIALIZED);
    assert(crack_u64(empty, 0, &out, 0) == BAD_SEARCH_LENGTH);

    CREATE_CONTEXT(ctx);
    assert(init_crack_ctx(ctx, FNV64_OFFSET_BASIS, FNV64_PRIME, 64, "ab", "pre", "suf"));
    uint64_t target = fnv_u64("preabsuf", FNV64_OFFSET_BASIS, FNV64_PRIME, 64);

    assert(crack_u64_with_len(ctx, target, &out, 8) == SUCCESS);
    assert(out.length == 8);
    clear_result(&out);

    assert(crack_u64_with_len(ctx, target, &out, 5) == BAD_SEARCH_LENGTH);

    assert(crack_u64(ctx, target, &out, 2) == SUCCESS);
    assert(out.length == 8);
    clear_result(&out);

    assert(crack_u64_limits(ctx, 0, &out, 0, CRACK_DEFAULT_ENUM_BOUND, 1) == FAILED);
    destroy_crack_ctx(ctx);

    fmpz_t offset, prime, ftarget;
    fmpz_init_set_ui(offset, FNV64_OFFSET_BASIS);
    fmpz_init_set_ui(prime, FNV64_PRIME);
    fmpz_init(ftarget);

    CREATE_CONTEXT(fempty);
    fmpz_zero(ftarget);
    assert(crack_fmpz(fempty, ftarget, &out, 1) == CONTEXT_UNINITIALIZED);

    CREATE_CONTEXT(fctx);
    assert(init_crack_fmpz_ctx(fctx, offset, prime, 128, "ab", "pre", "suf"));
    fnv_fmpz(ftarget, "preabsuf", offset, prime, 128);

    assert(crack_fmpz_with_len(fctx, ftarget, &out, 8) == SUCCESS);
    assert(out.length == 8);
    clear_result(&out);

    assert(crack_fmpz_with_len(fctx, ftarget, &out, 5) == BAD_SEARCH_LENGTH);

    assert(crack_fmpz(fctx, ftarget, &out, 2) == SUCCESS);
    assert(out.length == 8);
    clear_result(&out);

    assert(crack_fmpz_limits(fctx, ftarget, &out, 0, CRACK_DEFAULT_ENUM_BOUND, 0) == FAILED);

    fmpz_zero(ftarget);
    assert(crack_fmpz(fctx, ftarget, &out, 1) == FAILED);

    destroy_crack_ctx(fctx);
    fmpz_clear(ftarget);
    fmpz_clear(prime);
    fmpz_clear(offset);
}

static void check_result_malloc_failure(void) {
    const size_t prefix_len = 8 * 1024 * 1024;
    char* prefix = malloc(prefix_len);
    assert(prefix);
    memset(prefix, 'a', prefix_len);

    CREATE_CONTEXT(ctx);
    assert(init_crack_ctx_with_len(
        ctx,
        FNV64_OFFSET_BASIS,
        FNV64_PRIME,
        64,
        (char_buffer){NULL, 0},
        (char_buffer){prefix, prefix_len},
        (char_buffer){NULL, 0}
    ));
    uint64_t target = fnv_u64_with_len(
        (char_buffer){prefix, prefix_len},
        FNV64_OFFSET_BASIS,
        FNV64_PRIME,
        64
    );
    free(prefix);

    struct rlimit old_limit;
    if (limit_address_space(1024 * 1024, &old_limit)) {
        char_buffer out = {NULL, 0};
        assert(crack_u64_with_len(ctx, target, &out, (uint32_t)prefix_len) == MEMORY_ERROR);
        clear_result(&out);
        assert(setrlimit(RLIMIT_AS, &old_limit) == 0);
    }

    destroy_crack_ctx(ctx);
}

int main(void) {
    check_interrupt_api();
    check_inverse_api();
    check_fnv_api();
    check_context_api();
    check_crack_api();
    check_result_malloc_failure();
    return 0;
}
