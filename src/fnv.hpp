/*
* Minimal header file to make working with the fnv hashing algorithm as easy and flexible as possible
* NOTE: These hashing functions will convert all text to lowercase and convert all \ to / as this was originally made
* for Call of Duty hashes. If you want to support all characters, remove any parts that do `cur |= 0x20` or `cur = '/'` 
* 
* Example Usages:
*   using FNV_t = FNVUtilStatic<>; // will result in OFFSET_BASIS = 0xcbf29ce484222325, PRIME = 0x100000001b3, BIT_LEN = 64
*   using FNV_t = FNVUtilStatic<0x79D6530B0BB9B5D1>; // will result in OFFSET_BASIS = 0x79D6530B0BB9B5D1, PRIME = 0x100000001b3, BIT_LEN = 64
*   using FNV_t = FNVUtilStatic<0x79D6530B0BB9B5D1, 0x10000000233>; // will result in OFFSET_BASIS = 0x79D6530B0BB9B5D1, PRIME = 0x10000000233, BIT_LEN = 64
*
*   // using the last FNV example
*   uint64_t const_char_hashed = FNV_t::hash("test"); // => 0x81A14EF6F281AD49
*   std::string test = "test"; 
*   uint64_t std_string_hashed = FNV_t::hash(test); // => 0x81A14EF6F281AD49
*
*/

#pragma once
#include <cstdint>
#include <string>

template <uint64_t OFFSET_BASIS = 0xcbf29ce484222325, uint64_t PRIME = 0x100000001b3, uint32_t BIT_LEN = 64>
class FNVUtilStatic {
    // just to make sure
    static_assert(BIT_LEN <= 64, "The maximum value of BIT_LEN is 64");

public:
    static constexpr uint64_t hash(const char* string) {
        uint64_t hash = OFFSET_BASIS;
        constexpr uint64_t prime = PRIME;
        for (int i = 0; string[i]; ++i) {
            char cur = string[i];
            if (static_cast<unsigned char>(cur - 'A') <= 25)
                cur |= 0x20;
            else if (cur == '\\')
                cur = '/';

            hash ^= cur;
            hash *= prime;
        }

        if constexpr (BIT_LEN != 64) {
            return hash & ((1ULL << BIT_LEN) - 1);
        } else {
            return hash;
        }
    }

    static constexpr uint64_t hash(const std::string& string) {
        uint64_t hash = OFFSET_BASIS;
        constexpr uint64_t prime = PRIME;
        for (const char& chr : string) {
            char cur = chr;
            if (static_cast<unsigned char>(cur - 'A') <= 25)
                cur |= 0x20;
            else if (cur == '\\')
                cur = '/';

            hash ^= cur;
            hash *= prime;
        }

        if constexpr (BIT_LEN != 64) {
            return hash & ((1ULL << BIT_LEN) - 1);
        } else {
            return hash;
        }
    }
};

template <uint32_t BIT_LEN = 64>
class FNVUtil {
    // just to make sure
    static_assert(BIT_LEN <= 64, "The maximum value of BIT_LEN is 64");

public:
    static constexpr uint64_t hash(const char* string, const uint64_t OFFSET_BASIS, const uint64_t PRIME) {
        uint64_t hash = OFFSET_BASIS;
        for (int i = 0; string[i]; ++i) {
            char cur = string[i];
            if (static_cast<unsigned char>(cur - 'A') <= 25)
                cur |= 0x20;
            else if (cur == '\\')
                cur = '/';

            hash ^= cur;
            hash *= PRIME;
        }

        if constexpr (BIT_LEN != 64) {
            return hash & ((1ULL << BIT_LEN) - 1);
        } else {
            return hash;
        }
    }

    static constexpr uint64_t hash(const std::string& string, const uint64_t OFFSET_BASIS, const uint64_t PRIME) {
        uint64_t hash = OFFSET_BASIS;
        for (const char& chr : string) {
            char cur = chr;
            if (static_cast<unsigned char>(cur - 'A') <= 25)
                cur |= 0x20;
            else if (cur == '\\')
                cur = '/';

            hash ^= cur;
            hash *= PRIME;
        }

        if constexpr (BIT_LEN != 64) {
            return hash & ((1ULL << BIT_LEN) - 1);
        } else {
            return hash;
        }
    }
};