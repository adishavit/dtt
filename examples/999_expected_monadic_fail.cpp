// 999_expected_monadic_fail.cpp - Unsafe monadic operation patterns
// Tests that DTT catches unsafe access after monadic operations
// These patterns should trigger DTT errors

// Check for native std::expected via feature test macro
#if __has_include(<version>)
    #include <version>
#endif

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202211L
    #include <expected>
#else
    #include "../include/dtt_expected.h"
#endif

#include <string>

enum class Error { NotFound, Invalid };

std::expected<int, Error> parse(const std::string& s) {
    if (s.empty()) return std::unexpected{Error::Invalid};
    return 42;
}

std::expected<double, Error> to_double(int x) {
    return static_cast<double>(x);
}

// Variant 1: Accessing value after and_then on error - UNSAFE
void test_and_then_on_error_access_value() {
    std::expected<int, Error> e{std::unexpected{Error::NotFound}};
    auto result = e.and_then(to_double);
    // ERROR: e had error, so result has error, *result is UB
    double d = *result;  // DTT should catch this!  dtt-error {{_has_value}}
    (void)d;
}

int main() {
    test_and_then_on_error_access_value();
    return 0;
}
