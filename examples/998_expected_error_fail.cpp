// 998_expected_error_fail.cpp - Unsafe std::expected::error() call (UB)
// Tests that DTT catches calling error() when expected has a value
// Uses native std::expected if available (libstdc++ 12+, libc++ 16+, MSVC 2022)

#if __has_include(<expected>) && __cplusplus >= 202302L
    #include <expected>
#else
    #include "../include/dtt_expected.h"
#endif

enum class Error { NotFound, Invalid };

// Variant 1: error() with value
void test_error_with_value() {
    std::expected<int, Error> e{42};
    Error err = e.error();  // FAIL: calling error() with value is UB
    (void)err;
}

// Variant 2: After assignment to value
void test_error_after_value_assignment() {
    std::expected<int, Error> e{std::unexpected{Error::NotFound}};
    e = 42;  // Now has value
    Error err = e.error();  // FAIL: calling error() with value is UB
    (void)err;
}

// Variant 3: After emplace
void test_error_after_emplace() {
    std::expected<int, Error> e{std::unexpected{Error::Invalid}};
    e.emplace(100);  // Now has value
    Error err = e.error();  // FAIL: calling error() with value is UB
    (void)err;
}

int main() {
    test_error_with_value();
    test_error_after_value_assignment();
    test_error_after_emplace();
    return 0;
}
