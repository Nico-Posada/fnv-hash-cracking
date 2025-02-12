#include "my_print.hpp"
#include <cstdint>

#include "crack.hpp"

int main() {
    // this example shows how you can quickly find a collision for a given hash
    const uint64_t goal_hash = 0x1337133713371337;

    // here we're under the assumption that we have a known hash and dont care about recovering the original
    // string, we just care about finding an input of printable chars that results in this hash

    // using default fnv64-1a parameters
    using FNV_t = FNVUtilStatic<>;

    // setting valid charset and bruting charset to all printable chars
    auto crack = CrackUtils<>(presets::printable, presets::printable);

    // var to store result in if an input is found
    std::string result;

    // here im setting max crack length to 12, should basically guarantee a hit but might take a bit
    // longer if a valid result for string length <= 11 isnt found
    if (crack.brute_n(result, goal_hash, 12) == HASH_CRACKED) {
        print("Found collision! \"{}\" ({:#x})\n", result, FNV_t::hash(result));
    } else {
        print("Failed to find a valid input for goal hash ):\n");
    }
}