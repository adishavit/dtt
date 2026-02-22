// 808_std_shared_ptr_xpass.cpp
// Known false positives: safe shared_ptr usage that DTT incorrectly rejects.
// These are tracking gaps where DTT loses property information.

#include <memory>

// Variant 1: Assignment from make_shared to existing ptr doesn't update _nonnull
void test_reset_make_shared() {
    std::shared_ptr<int> ptr;
    ptr = std::make_shared<int>(42);
    *ptr;  // OK: non-null (DTT incorrectly reports _nonnull = 0)
}

// Variant 2: get() != nullptr guard doesn't establish _nonnull proof
void test_get_guard() {
    std::shared_ptr<int> ptr;
    if (ptr.get() != nullptr) {
        *ptr;  // OK: guarded (DTT doesn't propagate get() guard to _nonnull)
    }
}

// Variant 3: After swap, guard on original ptr doesn't work
void test_swap_then_guard() {
    auto p1 = std::make_shared<int>(1);
    std::shared_ptr<int> p2;
    p1.swap(p2);
    if (p1) {
        *p1;  // OK: guarded (DTT loses tracking through swap + guard)
    }
}

int main() {
    return 0;
}
