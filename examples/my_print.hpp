#pragma once
#include <iostream>

#if !defined(__cplusplus)
#   error "Why isnt __cplusplus defined"
#endif

#if __cplusplus >= 202302L  /* C++23 */
#include <format>
#include <print>
using namespace std::print;
#elif __cplusplus >= 202002L  /* C++20 */
#include <format>
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
