// 806_std_optional_operators_fail.cpp
// Tests: operator* and operator-> on empty optional - should FAIL

#include <optional>
#include <string>

// =============================================================================
// SECTION 1: operator* on empty optional
// =============================================================================

// Variant 1.1: operator* on default-constructed
void test_deref_empty() {
    std::optional<int> opt;
    *opt;  // ERROR: _has_value = 0
}

// Variant 1.2: operator* after reset
void test_deref_after_reset() {
    std::optional<int> opt = 42;
    opt.reset();
    *opt;  // ERROR: _has_value = 0
}

// =============================================================================
// SECTION 2: operator-> on empty optional
// =============================================================================

// Variant 2.1: operator-> on default-constructed
void test_arrow_empty() {
    std::optional<std::string> opt;
    opt->size();  // ERROR: _has_value = 0
}

// Variant 2.2: operator-> after reset
void test_arrow_after_reset() {
    std::optional<std::string> opt = "hello";
    opt.reset();
    opt->size();  // ERROR: _has_value = 0
}

int main() { return 0; }
