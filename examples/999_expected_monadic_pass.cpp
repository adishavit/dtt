// 999_expected_monadic_pass.cpp - Monadic operations tracking
// Tests that DTT tracks state through and_then/transform/or_else/transform_error
// These are safe patterns that DTT correctly allows

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

// Helper functions for monadic chaining
std::expected<int, Error> parse(const std::string& s) {
    if (s.empty()) return std::unexpected{Error::Invalid};
    return 42;
}

std::expected<double, Error> to_double(int x) {
    return static_cast<double>(x);
}

int double_it(int x) {
    return x * 2;
}

std::expected<int, Error> recover(Error e) {
    return 0;  // Default recovery value
}

Error log_error(Error e) {
    return Error::NotFound;  // Transform error type
}

// Variant 1: and_then preserves has_value state
void test_and_then_preserves_value() {
    std::expected<int, Error> e{42};
    auto result = e.and_then(to_double);
    // result should have value because e had value and to_double returns value
    double d = *result;  // SHOULD BE SAFE - DTT should track this
    (void)d;
}

// Variant 2: and_then on error stays error
void test_and_then_error_stays_error() {
    std::expected<int, Error> e{std::unexpected{Error::NotFound}};
    auto result = e.and_then(to_double);
    // result should NOT have value because e had error
    // Accessing error() should be safe
    Error err = result.error();  // SHOULD BE SAFE
    (void)err;
}

// Variant 3: transform preserves has_value state
void test_transform_preserves_value() {
    std::expected<int, Error> e{42};
    auto result = e.transform(double_it);
    // result has value because e had value
    int x = *result;  // SHOULD BE SAFE
    (void)x;
}

// Variant 4: transform on error stays error
void test_transform_error_stays_error() {
    std::expected<int, Error> e{std::unexpected{Error::NotFound}};
    auto result = e.transform(double_it);
    // result should NOT have value
    Error err = result.error();  // SHOULD BE SAFE
    (void)err;
}

// Variant 5: or_else can recover from error
void test_or_else_recovers() {
    std::expected<int, Error> e{std::unexpected{Error::NotFound}};
    auto result = e.or_else(recover);
    // result could have value if recover succeeded
    // This is complex - we can't know statically if recover returns value
    // For now, should be conservative and allow access after guard
    if (result.has_value()) {
        int x = *result;  // SHOULD BE SAFE - guarded
        (void)x;
    }
}

// Variant 6: or_else on value stays value
void test_or_else_value_stays_value() {
    std::expected<int, Error> e{42};
    auto result = e.or_else(recover);
    // result has value because e already had value (or_else not called)
    int x = *result;  // SHOULD BE SAFE
    (void)x;
}

// Variant 7: transform_error preserves error state
void test_transform_error_on_error() {
    std::expected<int, Error> e{std::unexpected{Error::Invalid}};
    auto result = e.transform_error(log_error);
    // result still has error (different error, but still error)
    Error err = result.error();  // SHOULD BE SAFE
    (void)err;
}

// Variant 8: transform_error on value stays value
void test_transform_error_on_value() {
    std::expected<int, Error> e{42};
    auto result = e.transform_error(log_error);
    // result has value because e had value (transform_error not called)
    int x = *result;  // SHOULD BE SAFE
    (void)x;
}

int main() {
    test_and_then_preserves_value();
    test_and_then_error_stays_error();
    test_transform_preserves_value();
    test_transform_error_stays_error();
    test_or_else_recovers();
    test_or_else_value_stays_value();
    test_transform_error_on_error();
    test_transform_error_on_value();
    return 0;
}
