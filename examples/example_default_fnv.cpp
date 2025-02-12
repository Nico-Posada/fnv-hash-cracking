#include "my_print.hpp"
#include <cstdint>

#include "crack.hpp"

int main()
{
    // if you're looking to crack a hash whose original value you don't
    // know the valid charset of, it's best to stick with <= 10 for max crack length
    // to not make cracking take stupid long (plus you'll probably just end up with a collision)
    std::string to_crack = "<=10 chars";

    // template defaults are the fnv64-1a default values
    const uint64_t hash = FNVUtilStatic<>::hash(to_crack);

    // same here, template defaults are the fnv64-1a default values.
    // here, the first arg is the valid charset (so if we find a result but all chars arent in the valid charset, we ignore it)
    // and the second arg is the bruting charset. In this example we use all printable chars, but know that the longer this charset, the
    // longer it'll take to crack hashes for longer strings
    auto crack = CrackUtils<>(presets::printable, presets::printable);

    // var to store result in
    std::string result;

    // note the use of 10, this function will try to crack the hash for string lengths [1, 10].
    // the larger this gets, the higher odds of it finding a collision are and the longer it'll take
    if (crack.brute_n(result, hash, 10) == HASH_CRACKED) {
        if (result == to_crack) {
            print("Success! Found \"{}\"\n", result);
        } else {
            print("Found collision! \"{}\"\n", result);
        }
    } else {
        print("Failed to crack hash ):\n");
    }
}