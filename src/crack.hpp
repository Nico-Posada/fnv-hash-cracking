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

    // generate a vector of all permutations of `chars` of length `repeat`
    // uses caching to prevent it from generating this list multiple times
    // TODO make thread safe to prevent it from generating redundant lists
    std::vector<std::string>& product(const std::string_view& chars, int repeat) {
        static std::unordered_map<int, std::vector<std::string>> cache;
        
        auto it = cache.find(repeat);
        if (it != cache.end())
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
        cache[repeat] = result;    
        return cache[repeat];
    }

public:
    explicit CrackUtils(const char* charset) : charset_selected{ true }, valid{ std::string(charset) } {}
    explicit CrackUtils(const std::string& charset) : charset_selected{ true }, valid{ charset } {}
    explicit CrackUtils() : charset_selected{ true }, valid{ presets::valid } {}

    void set_bruting_charset(const std::string& chars) {
        this->bruting_chars = chars;
    }
    
    bool try_crack_single(
        std::string& result,
        const uint64_t target,
        const uint32_t expected_len,
        const uint32_t brute = 0,
        const std::string& prefix = "",
        const std::string& suffix = ""
    ) {
        if (!this->charset_selected) {
            cout << "Never selected a valid charset!\n";
            return false;
        }

        Z_NR<mpz_t> MOD;
        mpz_ui_pow_ui(MOD.get_data(), 2U, BIT_LEN); // 2 ** BIT_LEN

        // change according to whatever youre working with
        const std::string& valid_charset = this->valid;

        const uint32_t nn = expected_len - brute - prefix.size() - suffix.size();
        const uint32_t dim = nn + 2;

        uint64_t P = 1;
        for (uint32_t i = 0; i < nn; ++i) {
            P *= PRIME;
        }
        
        if constexpr (BIT_LEN != 64) {
            P &= (1ULL << BIT_LEN) - 1;
        }

        Z_NR<mpz_t> start;
        mpz_set_ui(start.get_data(), 1ULL << 12); // 2 ** 12

        ZZ_mat<mpz_t> Q(dim, dim);
        Q(0, 0) = start;
        for (uint32_t i = 1; i < dim - 1; ++i)
            Q(i, i) = 1ULL << 4; // 2 ** 4
        Q(dim - 1, dim - 1) = 1ULL << 10; // 2 ** 10

        // identity matrix but with an extra column on the left and extra row on the bottom
        ZZ_mat<mpz_t> _M(dim, dim);
        for (uint32_t i = 0; i <= nn; ++i)
            _M(i, i+1) = 1;

        // fill in extra column on the left
        // (except second to last val)
        for (uint32_t i = 0; i < nn; ++i) {
            mpz_ui_pow_ui(_M(i, 0).get_data(), PRIME, nn - i);
        }
        _M(dim - 1, 0) = MOD;

        // perform reverse of fnv algo to get hash without suffix applied
        uint64_t ntarget = target;
        if (!suffix.empty()) {
            for (int32_t i = suffix.size() - 1; i >= 0; --i) {
                ntarget *= InverseHelper<PRIME, BIT_LEN>::inverse();
                ntarget ^= suffix.at(i);
            }
        }

        std::string ret = std::string();
        ret.reserve(0x10); // final crack length should never exceed this

        if constexpr (BIT_LEN != 64) {
            ntarget &= (1ULL << BIT_LEN) - 1;
        }

        // setup fnv class
        using FNV = FNVUtil<BIT_LEN>;
        const uint64_t prefixed_hash = FNV::hash(prefix, OFFSET_BASIS, PRIME);

        for (const auto& br : this->product(this->bruting_chars, brute)) {
            // get the hash without the prefix applied
            const uint64_t new_hash = FNV::hash(br, prefixed_hash, PRIME);

            uint64_t m = new_hash * P - ntarget;
            if constexpr (BIT_LEN != 64) {
                m &= (1ULL << BIT_LEN) - 1;
            }

            // create copy with (0, dim - 2) set
            ZZ_mat<mpz_t> M;
            M = _M;
            M(dim - 2, 0) = m;

            // M *= Q
            for (uint32_t x = 0; x < dim; ++x) {
                const auto& Q_val = Q(x, x).get_data();
                for (uint32_t y = 0; y < dim; ++y) {
                    auto& data = M(y, x).get_data();
                    mpz_mul(data, data, Q_val);
                }
            }

            // M = M.LLL()
            lll_reduction(M, LLL_DEF_DELTA, LLL_DEF_ETA, LM_HEURISTIC, FT_DOUBLE);

            // M /= Q
            for (uint32_t x = 0; x < dim; ++x) {
                const auto& Q_val = Q(x, x).get_data();
                for (uint32_t y = 0; y < dim; ++y) {
                    auto& data = M(y, x).get_data();
                    mpz_div(data, data, Q_val);
                }
            }

            for (uint32_t i = 0; i < dim; ++i) {
                const MatrixRow<Z_NR<mpz_t>>& row = M[i];

                const int32_t size = row.size();
                const int64_t row_last = mpz_get_si(row[size - 1].get_data());
                if (row_last != -1 && row_last != 1)
                    continue;

                ret.clear();

                bool success = true;
                const int32_t lo_hsh = new_hash & 0x7f;
                const int32_t lo_p = PRIME & 0x7f;
                int32_t a = lo_hsh;

                for (int32_t j = 1; j < size - 1; ++j) {
                    const int64_t cur = mpz_get_si(row[j].get_data()) * row_last;

                    const int32_t b = a;
                    a += cur;
                    const uint32_t x = a ^ b;
                    if (x >= 128 || valid_charset.find(x) == std::string::npos) {
                        success = false;
                        break;
                    }

                    ret += char(x);
                    a *= lo_p;
                }

                if (success) {
                    result = prefix + br + ret + suffix;
                    return true;
                }
            }
        }

        return false;
    }

    bool brute_n(
        string& result,
        const uint64_t target,
        const uint32_t max_search_len,
        const string& prefix = "",
        const string& suffix = ""
    ) {
        constexpr uint32_t MAX_CRACK_LEN = 8; // change depending on prime value
        const uint32_t known_len = static_cast<uint32_t>(prefix.size() + suffix.size());
        for (uint32_t n = 1 + known_len; n <= max_search_len + known_len; ++n) {
            const uint32_t brute_len = n <= MAX_CRACK_LEN + known_len ? 0 : n - known_len - MAX_CRACK_LEN;
            if (this->try_crack_single(result, target, n, brute_len, prefix, suffix))
                return true;
        }

        return false;
    }
};