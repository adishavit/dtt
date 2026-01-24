// 807_std_unique_ptr_operators_pass.cpp
// Tests: operator* and operator-> with supported guard patterns - should PASS

#include <memory>

// =============================================================================
// SECTION 1: if (ptr) guard with operator*
// =============================================================================

// Variant 1.1: if (ptr) guard then dereference
void test_if_guard_deref() {
    std::unique_ptr<int> ptr(new int(42));
    if (ptr) {
        *ptr = 100;  // OK: operator bool guard
    }
}

// Variant 1.2: !ptr early return
void test_not_ptr_early_return() {
    std::unique_ptr<int> ptr(new int(42));
    if (!ptr) return;
    *ptr = 100;  // OK: negated guard with early return
}

// =============================================================================
// SECTION 2: if (ptr) guard with operator->
// =============================================================================

struct S { int x; void set(int v) { x = v; } };

// Variant 2.1: if (ptr) guard then arrow
void test_if_guard_arrow() {
    std::unique_ptr<S> ptr(new S());
    if (ptr) {
        ptr->x = 42;  // OK: operator bool guard
    }
}

// Variant 2.2: !ptr early return then arrow
void test_not_ptr_arrow() {
    std::unique_ptr<S> ptr(new S());
    if (!ptr) return;
    ptr->set(100);  // OK: negated guard with early return
}

// =============================================================================
// SECTION 3: Constructor with non-null pointer
// =============================================================================

// Variant 3.1: 1-arg constructor sets _nonnull=1
void test_constructor_nonnull() {
    std::unique_ptr<int> ptr(new int(42));  // _nonnull = 1
    *ptr = 100;  // OK: constructor postcondition
}

// Variant 3.2: arrow after 1-arg constructor
void test_constructor_arrow() {
    std::unique_ptr<S> ptr(new S());  // _nonnull = 1
    ptr->x = 42;  // OK: constructor postcondition
}

int main() { return 0; }
