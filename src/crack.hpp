#pragma once
#include <cstdint>
#include <string>
#include <format>
#include <iomanip>

#include "helpers.hpp"
#include "fnv.hpp"
#include "defs.hpp"
#include "fplll.h"

using namespace fplll;

// for convenience only
namespace presets {
    static std::string printable = " !\"#$%&'()*+,-./0123456789:;<=>?@[]^_`abcdefghijklmnopqrstuvwxyz{|}~";
    static std::string alpha = "abcdefghijklmnopqrstuvwxyz";
    static std::string alphanum = "abcdefghijklmnopqrstuvwxyz0123456789";
    static std::string hex = "0123456789abcdef";
    static std::string ident = "abcdefghijklmnopqrstuvwxyz0123456789_";
}

enum CrackStatus {
    HASH_CRACKED,
    HASH_NOT_CRACKED,
    MISSING_CHARSET
};

template <uint64_t OFFSET_BASIS = OFFSET_DEFAULT, uint64_t PRIME = PRIME_1b3, uint32_t BIT_LEN = 64>
class CrackUtils {
    // just to make sure
    static_assert(BIT_LEN <= 64, "The maximum value of BIT_LEN is 64");

private:
    // list of characters which you know are valid for the hash you're trying to crack.
    // for example, if you're trying to crack a hex string, you know the final result will only
    // consist of chars 0-9a-f, so if we get a collision, we can properly skip it
    std::string valid_chars = "";

    // characters to use for brute forcing, the more you add the longer it'll take.
    // this is just the default list, can be changed with `set_bruting_charset`
    std::string bruting_chars = "0123456789abcdefghijklmnopqrstuvwxyz_.";

    // cache of character combinations for the brute force
    // TODO: use normal char arrays to improve memory efficiency
    std::unordered_map<int, std::vector<std::string>> cache;

    // generate a vector of all permutations of `chars` of length `repeat`
    // uses caching to prevent it from generating this list multiple times
    // TODO make thread safe to prevent it from generating redundant lists
    std::vector<std::string>& product(const std::string_view& chars, int repeat) {        
        auto it = this->cache.find(repeat);
        if (it != this->cache.end())
            return it->second;
        
        uint32_t vec_length = 1;
        for (int i = 0; i < repeat; ++i) {
            vec_length *= chars.size();
        }
        
        std::vector<std::string> result;
        result.reserve(vec_length);

        function<void(int, std::string&&)> generate = [&](int depth, std::string&& current) {
            if (depth == 0) {
                result.emplace_back(current);
                return;
            }
            
            for (const char c : chars)
                generate(depth - 1, std::move(current + c));
        };
        
        generate(repeat, std::move(std::string()));
        this->cache[repeat] = result;    
        return this->cache[repeat];
    }

public:
    explicit CrackUtils(const char* charset) : valid_chars{ std::string(charset) } {}
    explicit CrackUtils(const std::string& charset) : valid_chars{ charset } {}
    explicit CrackUtils() : valid_chars{ presets::printable } {}

    void set_bruting_charset(const std::string& chars) {
        this->bruting_chars = chars;
        this->cache.clear();
    }

    void set_bruting_charset(const std::string&& chars) {
        this->bruting_chars = std::move(chars);
        this->cache.clear();
    }

    void set_valid_charset(const std::string& chars) {
        this->valid_chars = chars;
    }

    void set_valid_charset(const std::string&& chars) {
        this->valid_chars = std::move(chars);
    }
    
    CrackStatus try_crack_single(
        std::string& result,
        const uint64_t target,
        const uint32_t expected_len,
        const uint32_t brute = 0,
        const std::string& prefix = "",
        const std::string& suffix = ""
    );

    CrackStatus brute_n(
        std::string& result,
        const uint64_t target,
        const uint32_t max_search_len,
        const std::string& prefix = "",
        const std::string& suffix = ""
    );
};

// function definitions in here
#define __CRACK_TPP
#include "crack.tpp"
#undef __CRACK_TPP