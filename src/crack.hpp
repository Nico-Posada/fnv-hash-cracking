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

    // generate a vector of all permutations of `chars` of length `repeat`
    // uses caching to prevent it from generating this list multiple times
    std::vector<std::string>& product(const std::string_view& chars, int repeat) {
        static std::unordered_map<int, std::vector<std::string>> cache;
        
        auto it = cache.find(repeat);
        if (it != cache.end())
            return it->second;
        
        std::vector<std::string> result;
        function<void(int, std::string)> generate = [&](int depth, std::string current) {
            if (depth == 0) {
                result.push_back(current);
                return;
            }
            
            for (const char c : chars)
                generate(depth - 1, current + c);
        };
        
        generate(repeat, "");
        cache[repeat] = result;    
        return cache[repeat];
    }

public:
    explicit CrackUtils(const char* charset) : charset_selected{ true }, valid{ std::string(charset) } {}
    explicit CrackUtils(const std::string& charset) : charset_selected{ true }, valid{ charset } {}
    explicit CrackUtils() : charset_selected{ true }, valid{ presets::valid } {}
    
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
        const std::string valid_charset = this->valid;

        const uint32_t nn = expected_len - brute - prefix.size() - suffix.size();
        const uint32_t dim = nn + 2;

        uint64_t P = 1;
        for (int i = 0; i < nn; ++i) {
            P *= PRIME;
        }
        
        if constexpr (BIT_LEN != 64) {
            P %= 1ULL << BIT_LEN;
        }

        Z_NR<mpz_t> start;
        mpz_set_ui(start.get_data(), 1ULL << 12); // 2 ** 12

        ZZ_mat<mpz_t> Q(dim, dim);
        Q(0, 0) = start;
        for (int i = 1; i < dim - 1; ++i)
            Q(i, i) = 1ULL << 4; // 2 ** 4
        Q(dim - 1, dim - 1) = 1ULL << 10; // 2 ** 10

        // identity matrix but with an extra column on the left and extra row on the bottom
        ZZ_mat<mpz_t> _M(dim, dim);
        for (int i = 0; i <= nn; ++i)
            _M(i, i+1) = 1;

        // fill in extra column on the left
        // (except second to last val)
        for (int i = 0; i < nn; ++i) {
            mpz_ui_pow_ui(_M(i, 0).get_data(), PRIME, nn - i);
        }
        _M(dim - 1, 0) = MOD;

        // perform reverse of fnv algo to get hash without suffix applied
        uint64_t ntarget = target;
        for (int i = suffix.size() - 1; i >= 0; --i) {
            ntarget *= InverseHelper<PRIME, BIT_LEN>::inverse();
            ntarget ^= suffix.at(i);
        }

        std::string ret = "";
        if constexpr (BIT_LEN != 64) {
            ntarget %= 1ULL << BIT_LEN;
        }

        // characters to use for brute forcing, the more you add the longer it'll take
        const std::string_view brute_chars = "0123456789abcdefghijklmnopqrstuvwxyz_.";

        // setup fnv class
        using FNV = FNVUtil<OFFSET_BASIS, PRIME, BIT_LEN>;

        for (const auto& br : this->product(brute_chars, brute)) {
            // get the hash without the prefix applied
            const std::string new_prefix = prefix + br;
            const uint64_t new_hash = FNV::hash(new_prefix);

            uint64_t m = new_hash * P - ntarget;
            if constexpr (BIT_LEN != 64) {
                m %= 1ULL << BIT_LEN;
            }

            // create copy with (0, dim - 2) set
            ZZ_mat<mpz_t> M;
            M = _M;
            M(dim - 2, 0) = m;

            // M *= Q
            for (int x = 0; x < dim; ++x) {
                auto& Q_val = Q(x, x).get_data();
                for (int y = 0; y < dim; ++y) {
                    auto& data = M(y, x).get_data();
                    mpz_mul(data, data, Q_val);
                }
            }

            // M = M.LLL()
            lll_reduction(M, LLL_DEF_DELTA, LLL_DEF_ETA, LM_HEURISTIC, FT_DOUBLE);

            // M /= Q
            for (int x = 0; x < dim; ++x) {
                auto& Q_val = Q(x, x).get_data();
                for (int y = 0; y < dim; ++y) {
                    auto& data = M(y, x).get_data();
                    mpz_div(data, data, Q_val);
                }
            }

            for (int i = 0; i < dim; ++i) {
                ret.clear();
                MatrixRow<Z_NR<mpz_t>> row = M[i];

                int size = row.size();
                int64_t row_last = mpz_get_si(row[size - 1].get_data());
                if (row_last != -1 && row_last != 1)
                    continue;

                bool success = true;
                int lo_hsh = new_hash & 0x7f;
                int lo_p = PRIME & 0x7f;
                int a = lo_hsh;

                for (int j = 1; j < size - 1; ++j) {
                    int64_t cur = mpz_get_si(row[j].get_data());
                    cur *= row_last;

                    int b = a;
                    a += cur;
                    uint32_t x = a ^ b;
                    if (x >= 128 || valid_charset.find(x) == std::string::npos) {
                        success = false;
                        break;
                    }

                    ret += char(x);
                    a *= lo_p;
                }

                if (success) {
                    result = new_prefix + ret + suffix;
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
        constexpr int MAX_CRACK_LEN = 8; // change depending on prime value
        const size_t known_len = prefix.size() + suffix.size();
        for (int n = 1 + known_len; n <= max_search_len + known_len; ++n) {
            const uint32_t brute_len = n <= MAX_CRACK_LEN + known_len ? 0 : n - known_len - MAX_CRACK_LEN;
            if (this->try_crack_single(result, target, n, brute_len, prefix, suffix))
                return true;
        }

        return false;
    }
};