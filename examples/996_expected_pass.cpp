// 996_expected_pass.cpp - Safe std::expected access patterns
// Tests that DTT allows correct expected usage

#include "../include/dtt_expected.h"

enum class Error { NotFound, Invalid };

// Variant 1: Guarded access with has_value()
void test_has_value_guard() {
    std::expected<int, Error> e{42};
    if (e.has_value()) {
        int x = *e;  // OK - guarded
        (void)x;
    }
}

// Variant 2: Guarded access with bool conversion
void test_bool_guard() {
    std::expected<int, Error> e{42};
    if (e) {
        int x = *e;  // OK - guarded
        int y = e.value();  // OK - guarded
        (void)x;
        (void)y;
    }
}

// Variant 3: Guarded error access with negation
void test_error_guard() {
    std::expected<int, Error> e{std::unexpected{Error::NotFound}};
    if (!e) {
        Error err = e.error();  // OK - guarded (no value)
        (void)err;
    }
}

// Variant 4: value_or is always safe
void test_value_or() {
    std::expected<int, Error> e{std::unexpected{Error::Invalid}};
    int x = e.value_or(0);  // OK - always safe
    (void)x;
}

// Variant 5: Constructed with value
void test_value_construction() {
    std::expected<int, Error> e{42};
    int x = *e;  // OK - constructed with value
    int y = e.value();  // OK
    (void)x;
    (void)y;
}

// Variant 6: Constructed with error, access error
void test_error_construction() {
    std::expected<int, Error> e{std::unexpected{Error::NotFound}};
    Error err = e.error();  // OK - constructed with error
    (void)err;
}

// Variant 7: Reassignment to value
void test_reassign_to_value() {
    std::expected<int, Error> e{std::unexpected{Error::NotFound}};
    e = 42;  // Now has value
    int x = *e;  // OK - has value now
    (void)x;
}

// Variant 8: Reassignment to error
void test_reassign_to_error() {
    std::expected<int, Error> e{42};
    e = std::unexpected{Error::Invalid};  // Now has error
    Error err = e.error();  // OK - has error now
    (void)err;
}

// Variant 9: Arrow operator with guard
struct Data { int val; };
void test_arrow_guard() {
    std::expected<Data, Error> e{Data{42}};
    if (e) {
        int x = e->val;  // OK - guarded
        (void)x;
    }
}

// Variant 10: emplace sets value
void test_emplace() {
    std::expected<int, Error> e{std::unexpected{Error::NotFound}};
    e.emplace(100);  // Now has value
    int x = *e;  // OK - emplace sets value
    (void)x;
}

int main() {
    test_has_value_guard();
    test_bool_guard();
    test_error_guard();
    test_value_or();
    test_value_construction();
    test_error_construction();
    test_reassign_to_value();
    test_reassign_to_error();
    test_arrow_guard();
    test_emplace();
    return 0;
}
