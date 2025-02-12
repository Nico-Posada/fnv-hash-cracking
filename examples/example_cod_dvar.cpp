#include "my_print.hpp"
#include <cstdint>

#include "crack.hpp"

int main() {
    // this is bit more of a niche example, but in call of duty all dvars must be strings with the chars
    // a-z0-9_# in them, so we can set that as our charset to try to crack dvar names or just to find funny collisions
    // that will work to set dvars
    const std::string charset = presets::ident + "#";

    // for this example i'll be going for a funny collision, but you can change this value to an
    // actual dvar value and see if you can crack it
    const uint64_t goal_dvar = 0x0123456789abcdef;

    // the offset is normally different and is even dependent on the first character of the string in recent titles
    // so ill just keep it as the default offset for this example
    auto crack = CrackUtils<OFFSET_DEFAULT, PRIME_233>(charset, charset);

    // i want my final output to have this prefix
    const std::string prefix = "wow_isnt_this_great#";

    // you can set this to whatever, but it'll take exponentially more time
    // if it does actually start bruting for strings of length 12
    constexpr uint32_t MAX_OUTPUT_LENGTH = 12;

    // var to store the result if we find a valid result 
    std::string result;

    if (crack.brute_n(result, goal_dvar, MAX_OUTPUT_LENGTH, prefix) == HASH_CRACKED) {
        print("Found a valid dvar input string! \"{}\" ({:#x})\n", result, FNVUtilStatic<OFFSET_DEFAULT, PRIME_233>::hash(result));
    } else {
        print("Failed to find a valid dvar input ):\n");
    }
}