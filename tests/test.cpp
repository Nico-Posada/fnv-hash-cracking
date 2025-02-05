#include <iostream>
#include <format>

#if !defined(__cplusplus)
#   error "Why isnt __cplusplus defined"
#endif

#if __cplusplus >= 202302L  /* C++23 */
#include <print>
using namespace std::print;
#elif __cplusplus >= 202002L  /* C++20 */
// convenience function since we're using c++20, not c++23 (which has std::print)
template<typename... Args>
void print(std::string_view fmt, Args&&... args) {
    std::vformat_to(
        std::ostreambuf_iterator<char>(std::cout),
        fmt,
        std::make_format_args(args...)
    );
}
#else
#   error "You must be using at least C++20"
#endif

#include "crack.hpp"

/* ======================================= */
/*                 EXAMPLES                */
/* ======================================= */

// using a known initial string just to test
bool example_1() {
    print("--- EXAMPLE 1 ---\n");
    using FNV_t = FNVUtilStatic<>;
    using CrackUtils_t = CrackUtils<>;

    // setup
    auto crack = CrackUtils_t();
    string to_hash = "known/crackmelol.exe";
    uint64_t hashed = FNV_t::hash(to_hash);

    // var to store cracked string if found
    string result;

    // max string length without needing to brute force is 8
    // since the string we want to crack is of length 10, we need 2 chars of brute force
    constexpr int BRUTE_CHARS = 2;

    // adding known prefix and suffix (useful if you're trying to crack a hashed filename)
    const string known_prefix = "known/";
    const string known_suffix = ".exe";
    
    // crack
    print("Trying to crack: {:#x}\n", hashed);
    if (crack.try_crack_single(result, hashed, to_hash.size(), BRUTE_CHARS, known_prefix, known_suffix)) {
        print("Found! {}\n", result);
    } else {
        print("Failed ):\n");
    }

    return result == to_hash;
}

// example of cracking hash without knowing what it is beforehand
bool example_2() {
    print("--- EXAMPLE 2 ---\n");
    using CrackUtils_t = CrackUtils<>;

    // setup
    auto crack = CrackUtils_t();

    // hashed string we want to crack
    uint64_t hashed = 0xC5BE054CB26B3829;

    // var to store cracked string if we find it
    string result;

    // max string length to try to crack
    constexpr int MAX_LEN = 10;

    // crack
    print("Trying to crack: {:#x}\n", hashed);

    // note the use of brute_n, this just runs try_crack_single with lengths [1, MAX_LEN]
    if (crack.brute_n(result, hashed, MAX_LEN)) {
        print("Found! {}\n", result);
    } else {
        print("Failed ):\n");
    }

    return result == "getalpha";
}

// cracking hash that's truncated to 63 bits which uses a different offset basis and prime
bool example_3() {
    print("--- EXAMPLE 3 ---\n");
    constexpr uint64_t OFFSET_BASIS = 0xE4A68FF7D4912FD2;
    constexpr uint64_t PRIME = PRIME_233;
    constexpr uint32_t BIT_LEN = 63;

    using FNV_t = FNVUtilStatic<OFFSET_BASIS, PRIME, BIT_LEN>;
    using CrackUtils_t = CrackUtils<OFFSET_BASIS, PRIME, BIT_LEN>;

    // setup (everything below is copied from the first example)
    auto crack = CrackUtils_t();
    string to_hash = "known/crackmelol.exe";
    uint64_t hashed = FNV_t::hash(to_hash);

    // var to store cracked string if found
    string result;

    // max string length without needing to brute force is 8
    // since the string we want to crack is of length 10, we need 2 chars of brute force
    constexpr int BRUTE_CHARS = 2;

    // adding known prefix and suffix (useful if you're trying to crack a hashed filename)
    const string known_prefix = "known/";
    const string known_suffix = ".exe";
    
    // crack
    print("Trying to crack: {:#x}\n", hashed);
    if (crack.try_crack_single(result, hashed, to_hash.size(), BRUTE_CHARS, known_prefix, known_suffix)) {
        print("Found! {}\n", result);
    } else {
        print("Failed ):\n");
    }

    return result == to_hash;
}

// cracking hash and using a character list to help avoid returning weird collisions
bool example_4() {
    print("--- EXAMPLE 4 ---\n");

    using FNV_t = FNVUtilStatic<>;
    using CrackUtils_t = CrackUtils<>;

    // list of valid characters
    string charset = "!?0123456789abcdef";

    // you can also use some of the presets defined in crack.hpp
    /*
    namespace presets {
        static std::string printable = " !\"#$%&'()*+,-./0123456789:;<=>?@[]^_`abcdefghijklmnopqrstuvwxyz{|}~";
        static std::string alpha = "abcdefghijklmnopqrstuvwxyz";
        static std::string alphanum = "abcdefghijklmnopqrstuvwxyz0123456789";
        static std::string hex = "0123456789abcdef";
        static std::string ident = "abcdefghijklmnopqrstuvwxyz0123456789_"
    }

    Ex: string charset = presets::valid_file;
    */

    // setup
    auto crack = CrackUtils_t(charset);

    // you can set the characters using in the brute force section too
    crack.set_bruting_charset(charset);

    string to_hash = "abc9784de!";
    uint64_t hashed = FNV_t::hash(to_hash);

    // var to store cracked string if found
    string result;

    // max string length without needing to brute force is 8
    // since the string we want to crack is of length 10, we need 2 chars of brute force
    constexpr int BRUTE_CHARS = 2;
    
    // crack
    print("Trying to crack: {:#x}\n", hashed);
    if (crack.try_crack_single(result, hashed, to_hash.size(), BRUTE_CHARS)) {
        print("Found! {}\n", result);
    } else {
        print("Failed ):\n");
    }

    return result == to_hash;
}

/* ======================================= */
/*                TEST CASES               */
/* ======================================= */
// these wont be commented like the example cases above

#define START_TEST_CASES(name) static vector<pair<string, function<bool()>>> name = {
#define END_TEST_CASES() };

#define TEST(func_name) \
    { #func_name, []() -> bool {
#define END_TEST() }},

START_TEST_CASES(test_cases)

TEST(test_changing_brute_charset)
    using FNV_t = FNVUtilStatic<>;
    using CrackUtils_t = CrackUtils<>;

    string to_hash = "abcdefghi";
    uint64_t hashed = FNV_t::hash(to_hash);

    auto crack = CrackUtils_t();
    crack.set_bruting_charset("0123456789");

    string result;

    if (crack.brute_n(result, hashed, 9))
    {
        print("Found result '{}' when it should have found none!\n", result);
        return false;
    }

    crack.set_bruting_charset(presets::alpha);
    if (crack.brute_n(result, hashed, 9))
    {
        bool ret = result == to_hash;
        if (!ret) {
            print("Found result '{}' but it was incorrect! {:#x} vs {:#x}\n", result, FNV_t::hash(result), hashed);
        }
        return ret;
    }

    print("Found no result after setting bruting charset!\n");
    return false;
END_TEST()

TEST(test_different_offset_basis)
    constexpr uint64_t OFFSET = 0xdeadbeef;
    using CrackUtils_t = CrackUtils<OFFSET>;
    using FNV_t = FNVUtilStatic<OFFSET>;

    string to_hash = "plswork!!!";
    uint64_t hashed = FNV_t::hash(to_hash);

    CrackUtils_t crack = CrackUtils_t(presets::alpha + "!");
    crack.set_bruting_charset(presets::alpha + "!");

    string result;
    if (!crack.brute_n(result, hashed, 10))
    {
        print("Failed to find hash when given different offset basis!\n");
        return false;
    }

    if (result != to_hash)
    {
        print("Found result '{}', but wasn't expected result! {:#x} vs {:#x}\n", result, FNV_t::hash(result), hashed);
        return false;
    }

    return true;
END_TEST()

END_TEST_CASES()

// run tests/examples
int main() {
    uint32_t total_cases = test_cases.size();
    uint32_t passed_cases = 0;
    vector<string> failed_cases{};

    for (const auto& [func_name, test_case] : test_cases) {
        print("Running {}\n", func_name);
        if (test_case()) {
            passed_cases++;
            print("Success!\n");
        } else {
            failed_cases.emplace_back(func_name);
        }
    }

    print("\nTEST RESULTS\nTotal Tests: {}\nPassed: {}\nFailed: {}\n",
          total_cases, passed_cases, failed_cases.size());
    
    if (!failed_cases.empty()) {
        print("\nFailed test cases:\n");
        for (const auto& name : failed_cases) {
            print("{}\n", name);
        }

        return 1;
    }

    return 0;
}