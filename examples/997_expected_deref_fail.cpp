// 997_expected_deref_fail.cpp - Unsafe std::expected dereference (UB)
// Tests that DTT catches dereferencing expected without value

#include "../include/dtt_expected.h"

enum class Error { NotFound, Invalid };

// Variant 1: Dereference without value
void test_deref_without_value() {
    std::expected<int, Error> e{std::unexpected{Error::NotFound}};
    int x = *e;  // FAIL: dereferencing expected without value is UB
    (void)x;
}

// Variant 2: Arrow operator without value
struct Data { int val; };
void test_arrow_without_value() {
    std::expected<Data, Error> e{std::unexpected{Error::Invalid}};
    int x = e->val;  // FAIL: using operator-> without value is UB
    (void)x;
}

// Variant 3: After reassignment to error
void test_after_error_assignment() {
    std::expected<int, Error> e{42};
    e = std::unexpected{Error::NotFound};  // Now has error
    int x = *e;  // FAIL: dereferencing expected without value is UB
    (void)x;
}

int main() {
    test_deref_without_value();
    test_arrow_without_value();
    test_after_error_assignment();
    return 0;
}
