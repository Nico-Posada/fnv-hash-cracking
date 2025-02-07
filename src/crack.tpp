#include "crack.hpp"
#ifndef __CRACK_TPP
#   error "crack.tpp is a template definition file for crack.hpp, do not use it outside of that"
#endif

template <uint64_t OFFSET_BASIS, uint64_t PRIME, uint32_t BIT_LEN>
CrackStatus CrackUtils<OFFSET_BASIS, PRIME, BIT_LEN>::try_crack_single(
    std::string& result,
    const uint64_t target,
    const uint32_t expected_len,
    const uint32_t brute, /* default is 0 */
    const std::string& prefix, /* default is "" */
    const std::string& suffix /* default is "" */
) {
    if (this->valid_chars.empty()) {
        cerr << "Never selected a charset of valid characters!\n";
        return MISSING_CHARSET;
    }

    if (this->bruting_chars.empty()) {
        cerr << "Never selected a charset of bruting characters!\n";
        return MISSING_CHARSET;
    }

    Z_NR<mpz_t> MOD;
    mpz_ui_pow_ui(MOD.get_data(), 2U, BIT_LEN); // 2 ** BIT_LEN

    const uint32_t nn = expected_len - brute - prefix.size() - suffix.size();
    const uint32_t dim = nn + 2;

    uint64_t P = 1;
    for (uint32_t i = 0; i < nn; ++i) {
        P *= PRIME;
    }
    
    if constexpr (BIT_LEN != 64) {
        P &= (1ULL << BIT_LEN) - 1;
    }

    // using VLA instead of malloc
    uint64_t Q[dim]{};
    Q[0] = 1ULL << 12; // 2 ** 12
    for (uint32_t i = 1; i < dim - 1; ++i) {
        Q[i] = 1ULL << 4; // 2 ** 4
    }
    Q[dim - 1] = 1ULL << 10; // 2 ** 10

    // identity matrix but with an extra column on the left and extra row on the bottom
    ZZ_mat<mpz_t> _M(dim, dim);
    for (uint32_t i = 0; i <= nn; ++i) {
        _M(i, i+1) = 1;
    }

    // fill in extra column on the left
    // (except second to last val)
    for (uint32_t i = 0; i < nn; ++i) {
        mpz_ui_pow_ui(_M(i, 0).get_data(), PRIME, nn - i);
    }
    _M(dim - 1, 0) = MOD;

    // M *= Q (part 1)
    // this should be done on every iteration, but since we only change one element in the M matrix
    // on each iteration, we can precompute almost everything else
    for (uint32_t x = 0; x < dim; ++x) {
        const uint64_t Q_val = Q[x];
        for (uint32_t y = 0; y < dim; ++y) {
            auto& data = _M(y, x).get_data();
            mpz_mul_ui(data, data, Q_val);
        }
    }

    // perform reverse of fnv algo to get hash without suffix applied
    uint64_t ntarget = target;
    if (!suffix.empty()) {
        constexpr uint64_t INV_PRIME = InverseHelper<PRIME, BIT_LEN>::inverse();
        for (int32_t i = suffix.size() - 1; i >= 0; --i) {
            ntarget *= INV_PRIME;
            ntarget ^= suffix.at(i);
        }
    }

    std::string ret = std::string();

    if constexpr (BIT_LEN != 64) {
        ntarget &= (1ULL << BIT_LEN) - 1;
    }

    // set up fnv class
    using FNV_t = FNVUtil<BIT_LEN>;
    const uint64_t prefixed_hash = FNV_t::hash(prefix, OFFSET_BASIS, PRIME);

    for (const auto& br : this->product(this->bruting_chars, brute)) {
        // get the hash without the prefix applied
        const uint64_t new_hash = FNV_t::hash(br, prefixed_hash, PRIME);

        uint64_t m = new_hash * P - ntarget;
        if constexpr (BIT_LEN != 64) {
            m &= (1ULL << BIT_LEN) - 1;
        }

        // create copy with (0, dim - 2) set
        ZZ_mat<mpz_t> M;
        M = _M;
        M(dim - 2, 0) = m;

        // M *= Q (part 2)
        mpz_mul_ui(M(dim - 2, 0).get_data(), M(dim - 2, 0).get_data(), Q[0]);

        // M = M.LLL()
        lll_reduction(M, LLL_DEF_DELTA, LLL_DEF_ETA, LM_HEURISTIC, FT_DOUBLE);

        // M /= Q
        for (uint32_t x = 0; x < dim; ++x) {
            const uint64_t Q_val = Q[x];
            for (uint32_t y = 0; y < dim; ++y) {
                auto& data = M(y, x).get_data();
                mpz_div_ui(data, data, Q_val);
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
                if (x >= 128 || this->valid_chars.find(x) == std::string::npos) {
                    success = false;
                    break;
                }

                ret += char(x);
                a *= lo_p;
            }

            if (success) {
                const string possible_result = prefix + br + ret + suffix;

                // double check to confirm we have a valid hash. sometimes this can
                // get false positives we need to ignore
                const uint64_t hashed_result = FNV_t::hash(possible_result, OFFSET_BASIS, PRIME);
                if (hashed_result == target)
                {
                    result = std::move(possible_result);
                    return HASH_CRACKED;
                }

                cerr << std::format("Got a false positive '{}' ({:#x} vs {:#x})\n", possible_result, hashed_result, target);
            }
        }
    }

    return HASH_NOT_CRACKED;
}

template <uint64_t OFFSET_BASIS, uint64_t PRIME, uint32_t BIT_LEN>
CrackStatus CrackUtils<OFFSET_BASIS, PRIME, BIT_LEN>::brute_n(
    std::string& result,
    const uint64_t target,
    const uint32_t max_search_len,
    const std::string& prefix, /* default is "" */
    const std::string& suffix  /* default is "" */
) {
    // The higher this value gets, the less accurate results will become.
    // For the default fnv hash values, 8 averages a 93% success rate while 9 dips it down to ~50%.
    // Setting it to 7 is 100% accurate in my tests, but you're trading off 7% accuracy for it being (len bruteset)x slower
    // when trying to crack longer hashes, so pick your poison.
    // TODO make this a class member variable, no need to hide it here
    constexpr uint32_t MAX_CRACK_LEN = 8;

    const uint32_t known_len = static_cast<uint32_t>(prefix.size() + suffix.size());
    for (uint32_t n = 1 + known_len; n <= max_search_len + known_len; ++n) {
        const uint32_t brute_len = n <= MAX_CRACK_LEN + known_len ? 0 : n - known_len - MAX_CRACK_LEN;
        CrackStatus ret = this->try_crack_single(result, target, n, brute_len, prefix, suffix);
        if (ret == HASH_NOT_CRACKED)
            continue;

        return ret;
    }

    return HASH_NOT_CRACKED;
}