// 808: shared_ptr assignment — unsafe patterns
// Tests that operator= from default/null shared_ptr tracks nullable state.

#include <memory>

// Variant 1: reset to null, then dereference
void test_reset_then_deref() {
    auto ptr = std::make_shared<int>(42);
    ptr.reset();  // Reset to null
    *ptr;  // FAIL: ptr is null after reset
}

// Variant 2: self-reset via assignment
void test_reset_via_assign() {
    std::shared_ptr<int> ptr;
    *ptr;  // FAIL: default-constructed is null
}

int main() { return 0; }
