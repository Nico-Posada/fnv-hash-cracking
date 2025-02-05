#pragma once
#include <cstdint>
#include <string>
#include <iomanip>

#include "helpers.hpp"
#include "fnv.hpp"
#include "defs.hpp"
#include "fplll.h"

using namespace fplll;

// for convenience only
namespace presets {
    static std::string valid = "0123456789abcdefghijklmnopqrstuvwxyz!\"#$%&'()*+,-./:;<=>?@[]^_`{|}~ ";
    static std::string valid_func = "0123456789abcdefghijklmnopqrstuvwxyz_";
    static std::string valid_file = "0123456789abcdefghijklmnopqrstuvwxyz_./";
    static std::string valid_gsc = "0123456789abcdefghijklmnopqrstuvwxyz_./:";
    static std::string valid_hex = "0123456789abcdef";
}

template <uint64_t OFFSET_BASIS = OFFSET_DEFAULT, uint64_t PRIME = PRIME_1b3, uint32_t BIT_LEN = 64>
class CrackUtils {
    // just to make sure
    static_assert(BIT_LEN <= 64, "The maximum value of BIT_LEN is 64");

private:
    bool charset_selected = false;
    std::string valid = "";

    // characters to use for brute forcing, the more you add the longer it'll take.
    // this is just the default list, can be changed with `set_bruting_charset`
    std::string_view bruting_chars = "0123456789abcdefghijklmnopqrstuvwxyz_.";

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
    explicit CrackUtils(const char* charset) : charset_selected{ true }, valid{ std::string(charset) } {}
    explicit CrackUtils(const std::string& charset) : charset_selected{ true }, valid{ charset } {}
    explicit CrackUtils() : charset_selected{ true }, valid{ presets::valid } {}

    void set_bruting_charset(const std::string& chars) {
        this->bruting_chars = chars;
        this->cache.clear();
    }
    
    bool try_crack_single(
        std::string& result,
        const uint64_t target,
        const uint32_t expected_len,
        const uint32_t brute = 0,
        const std::string& prefix = "",
        const std::string& suffix = ""
    );

    bool brute_n(
        string& result,
        const uint64_t target,
        const uint32_t max_search_len,
        const string& prefix = "",
        const string& suffix = ""
    );
};

// function definitions in here
#include "crack.tpp"