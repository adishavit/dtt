// 808: shared_ptr assignment postcondition propagation
// Tests that operator= propagates _nonnull from make_shared/other shared_ptr.

#include <memory>

// Variant 1: assign from make_shared
void test_assign_make_shared() {
    std::shared_ptr<int> ptr;
    ptr = std::make_shared<int>(42);
    *ptr;  // Safe: make_shared always returns non-null
}

// Variant 2: copy-assign from non-null shared_ptr
void test_copy_assign() {
    auto src = std::make_shared<int>(10);
    std::shared_ptr<int> dst;
    dst = src;
    *dst;  // Safe: src was non-null
}

int main() { return 0; }
