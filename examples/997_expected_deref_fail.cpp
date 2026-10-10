// 997_expected_deref_fail.cpp - Unsafe std::expected dereference (UB)
// Tests that DTT catches dereferencing expected without value
// Uses native std::expected if available (libstdc++ 12+, libc++ 16+, MSVC 2022)

// Check for native std::expected via feature test macro
#if __has_include(<version>)
    #include <version>
#endif

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202211L
    #include <expected>
#else
    #include "../include/dtt_expected.h"
#endif

enum class Error { NotFound, Invalid };

// Variant 1: Dereference without value
void test_deref_without_value() {
    std::expected<int, Error> e{std::unexpected{Error::NotFound}};
    int x = *e;  // FAIL: dereferencing expected without value is UB  dtt-error {{_has_value == 1}}
    (void)x;
}

// Variant 2: Arrow operator without value
struct Data { int val; };
void test_arrow_without_value() {
    std::expected<Data, Error> e{std::unexpected{Error::Invalid}};
    int x = e->val;  // FAIL: using operator-> without value is UB  dtt-error {{_has_value == 1}}
    (void)x;
}

// Variant 3: After reassignment to error
void test_after_error_assignment() {
    std::expected<int, Error> e{42};
    e = std::unexpected{Error::NotFound};  // Now has error
    int x = *e;  // FAIL: dereferencing expected without value is UB  dtt-error {{_has_value == 1}}
    (void)x;
}

int main() {
    test_deref_without_value();
    test_arrow_without_value();
    test_after_error_assignment();
    return 0;
}
