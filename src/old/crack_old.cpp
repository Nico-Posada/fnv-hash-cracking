/*
*
* This is just an old file I had for testing, it still has some code I want to port for multitheading
* so it will not yet be deleted
*
*/


/*
#include "fplll.h"
#include <iomanip>

// changes a lot depending on what you're trying to crack
constexpr uint64_t OFFSET_BASIS = 0x79D6530B0BB9B5D1;

// These are the only two primes COD uses (as far as I know)
constexpr uint64_t PRIME = 0x10000000233; // will crack 8 char plaintext with around 54% accuracy
// constexpr uint64_t PRIME = 0x100000001B3; // will crack 8 char plaintext with around 93% accuracy

// may occasionally change to 63
constexpr uint32_t BIT_LEN = 64;
static_assert(BIT_LEN <= 64, "The hard maximum on the BIT_LEN value is 64");

uint64_t fnv64(const char* string) {
    uint64_t hash = OFFSET_BASIS;
    constexpr uint64_t prime = PRIME;
    for (int i = 0; string[i] && string[i] != '\n'; ++i) {
        char cur = string[i];
        if ((unsigned char)(cur - 'A') <= 25)
            cur |= 0x20;

        if (cur == '\\')
            cur = '/';

        hash ^= cur;
        hash *= prime;
    }

    if constexpr (BIT_LEN != 64) {
        return hash % (1ULL << BIT_LEN);
    } else {
        return hash;
    }
}

uint64_t fnv64(const string& string) {
    uint64_t hash = OFFSET_BASIS;
    constexpr uint64_t prime = PRIME;
    for (const char& chr : string) {
        char cur = chr;
        if ((unsigned char)(cur - 'A') <= 25)
            cur |= 0x20;

        if (cur == '\\')
            cur = '/';

        hash ^= cur;
        hash *= prime;
    }

    if constexpr (BIT_LEN != 64) {
        return hash % (1ULL << BIT_LEN);
    } else {
        return hash;
    }
}

std::tuple<Z_NR<mpz_t>, Z_NR<mpz_t>, Z_NR<mpz_t>>
gcd_extended(Z_NR<mpz_t> a, Z_NR<mpz_t> b) {
    if (a == 0) {
        Z_NR<mpz_t> ra, rb;
        mpz_set_ui(ra.get_data(), 0UL);
        mpz_set_ui(rb.get_data(), 1UL);
        return make_tuple(b, ra, rb);
    }

    Z_NR<mpz_t> tmp_b, x;
    tmp_b.mod(b, a);
    auto [gcd, x1, y1] = gcd_extended(tmp_b, a);
    mpz_div(b.get_data(), b.get_data(), a.get_data());
    b.mul(b, x1);
    x.sub(y1, b);
    return make_tuple(gcd, x, x1);
}

template <unsigned long long prime, uint32_t exp>
uint64_t inverse() {
    static once_flag flag{};
    static uint64_t result;
    call_once(flag, [&]() {
        Z_NR<mpz_t> mpz_exp, mpz_prime, tmp;
        mpz_ui_pow_ui(mpz_exp.get_data(), 2U, exp);
        mpz_set_ui(mpz_prime.get_data(), prime);

        auto ret = gcd_extended(mpz_prime, mpz_exp);
        tmp.mod(std::get<1>(ret), mpz_exp);
        result = mpz_get_ui(tmp.get_data());
        if constexpr (exp != 64) {
            result %= 1ULL << exp;
        }
    });

    return result;
}

vector<string>& product(const string_view& chars, int repeat) {
    static unordered_map<int, vector<string>> cache;
    
    auto it = cache.find(repeat);
    if (it != cache.end())
        return it->second;
    
    vector<string> result;
    function<void(int, string)> generate = [&](int depth, string current) {
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

inline Z_NR<mpz_t> pow(const Z_NR<mpz_t>& base, unsigned int exponent) {
    Z_NR<mpz_t> result;
    mpz_pow_ui(result.get_data(), base.get_data(), exponent);
    return result;
}

bool solve(
    string& result,
    const uint64_t target,
    const uint32_t expected_len,
    const uint32_t brute = 0,
    const string& prefix = "",
    const string& suffix = ""
) {
    Z_NR<mpz_t> MOD, p;
    mpz_ui_pow_ui(MOD.get_data(), 2U, BIT_LEN); // 2 ** BIT_LEN
    mpz_set_ui(p.get_data(), PRIME);

    // change according to whatever youre working with
    const string valid_charset = valid_func;

    const uint32_t nn = expected_len - brute - prefix.size() - suffix.size();
    const uint32_t dim = nn + 2;

    uint64_t P = 1;
    for (int i = 0; i < nn; ++i)
        P *= PRIME;
    
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
    for (int i = 0; i < nn; ++i)
        _M(i, 0) = pow(p, nn - i);
    _M(dim - 1, 0) = MOD;

    uint64_t ntarget = target;
    for (int i = suffix.size() - 1; i >= 0; --i) {
        ntarget *= inverse<PRIME, BIT_LEN>();
        ntarget ^= suffix.at(i);
    }

    string ret = "";
    if constexpr (BIT_LEN != 64) {
        ntarget %= 1ULL << BIT_LEN;
    }

    // characters to use for brute forcing, the more you add the longer it'll take
    const string_view brute_chars = "0123456789abcdefghijklmnopqrstuvwxyz_.";

    for (const auto& br : product(brute_chars, brute)) {
        const string new_prefix = prefix + br;
        const uint64_t new_hash = fnv64(new_prefix);

        uint64_t m = new_hash * P - ntarget;
        if constexpr (BIT_LEN != 64)
            m %= 1ULL << BIT_LEN;

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
                if (x >= 128 || valid_charset.find(x) == string::npos) {
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
    constexpr int MAX_CRACK_LEN = 7; // change depending on prime value
    const size_t known_len = prefix.size() + suffix.size();
    for (int n = 1 + known_len; n <= max_search_len + known_len; ++n) {
        const uint32_t brute_len = n <= MAX_CRACK_LEN + known_len ? 0 : n - known_len - MAX_CRACK_LEN;
        if (solve(result, target, n, brute_len, prefix, suffix))
            return true;
    }

    return false;
}

void benchmark(string filename) {
    ifstream sample(filename);
    if (!sample.is_open()) {
        cout << "Failed to open input file\n";
        return;
    }

    auto start = chrono::high_resolution_clock::now();
    printf("Beginning benchmark...\n");
    string line;
    uint32_t success = 0, total = 0, collision = 0;
    while (getline(sample, line)) {
        // uint64_t hashed = fnv64(line);
        uint64_t hashed = strtoull(line.c_str(), nullptr, 16);
        string result;
        if (brute_n(result, hashed, 7)) {
            // if (result == line)
                success++;
            // else
                // collision++;
        }

        total++;
    }

    auto end = chrono::high_resolution_clock::now();
    chrono::duration<double> duration = end - start;

    printf("Success Collision Total\n%-8u%-10u%-5u\n", success, collision, total);
    printf("Execution time: %lf seconds\n", duration.count());
}

static vector<uint64_t> hashes;
bool init_hash_list(string filename) {
    ifstream sample(filename);
    if (!sample.is_open()) {
        cout << "Failed to open input file\n";
        return false;
    }

    string line;
    while (getline(sample, line)) {
        uint64_t num = strtoull(line.c_str(), nullptr, 16);
        // uint64_t num = fnv64(line);
        hashes.emplace_back(num);
    }

    return true;
}

bool next_hash(uint64_t& out) {
    static atomic_uint32_t hash_idx = 0;
    static mutex _mut;
    std::lock_guard<mutex> lock(_mut);
    if (hash_idx >= hashes.size())
        return false;

    out = hashes.at(hash_idx.load());
    hash_idx++;
    return true;
}

void write_data(const string& cracked, const uint64_t out, ofstream& file) {
    static mutex _mut;
    std::lock_guard<mutex> lock(_mut);
    file << "{ 0x" << hex << uppercase << setw(16) << setfill('0') << out << ", \"" << cracked << "\" }," << endl;
}

static atomic_uint32_t success = 0;
bool crack_single(ofstream& file) {
    uint64_t hash;
    if (!next_hash(hash))
        return false;

    string result;
    if (brute_n(result, hash, 10, "")) {
        cout << (result + "\n");
        write_data(result, hash, file);
    }
    return true;
}

void multithread(string filename, ofstream& file) {
    auto threads = thread::hardware_concurrency();
    thread_pool::thread_pool tp(threads);

    if (!init_hash_list(filename))
        return;

    function<void()> fn = [&file]() {
        while(crack_single(file))
            ;
    };

    for (int i = 0; i < threads; ++i)
        tp.push(fn);

    tp.wait_work();
    printf("Finished! %d\n", success.load());
}

int main(int argc, char** argv, char** envp) {
    // benchmark("uniq_gsc.txt");
    ofstream output("gsc_cracked.txt");
    multithread("new_gsc.txt", output);
    return 0;

    // string result;
    // uint64_t target = fnv64("test");
    // printf("Target => 0x%016lX\n", target);
    // if (brute_n(result, target, 10, "", "")) {
    //     cout << "Found! " << result << '\n';
    //     printf("0x%016lX\n", fnv64(result));
    // } else {
    //     cout << "Failed :(\n";
    // }

    // return 0;
}
*/