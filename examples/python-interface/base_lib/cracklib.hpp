#pragma once
#include <cstdio>
#include <string>
#include <fstream>
#include <cstdint>
#include <format>

#ifndef _OFFSET
#   error "_OFFSET must be defined when compiling this library"
#endif
#ifndef _PRIME
#   error "_PRIME must be defined when compiling this library"
#endif
#ifndef _BITS
#   error "_BITS must be defined when compiling this library"
#endif

#include <crack.hpp>

extern "C" {
    typedef CrackUtils<_OFFSET, _PRIME, _BITS> CrackUtils_t;
    extern CrackUtils_t crack;
    extern bool has_init;

    void init(const char* valid_chars, const char* bruting_chars);
    bool crack_hash(
        uint64_t hash,
        char* ret_buffer,
        size_t ret_buffer_size,
        uint32_t max_brute);
}