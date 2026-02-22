// 808: shared_ptr guard patterns — all should compile clean
// Tests that bool call guards (has_value, operator bool, get) properly
// establish proofs that satisfy dereference preconditions.

#include <memory>

// Variant 1: get() != nullptr guard establishes _nonnull proof
void test_get_guard() {
    std::shared_ptr<int> ptr;
    if (ptr.get() != nullptr) {
        *ptr;  // Safe: get() != nullptr proves _nonnull
    }
}

// Variant 2: operator bool guard after swap
void test_swap_then_guard() {
    auto p1 = std::make_shared<int>(1);
    std::shared_ptr<int> p2;
    p1.swap(p2);
    if (p1) {
        *p1;  // Safe: operator bool guard re-establishes _nonnull
    }
}

// Variant 3: if(ptr) guard on default-constructed shared_ptr
void test_bool_guard() {
    std::shared_ptr<int> ptr;
    if (ptr) {
        *ptr;  // Safe: operator bool guard
    }
}

int main() { return 0; }
