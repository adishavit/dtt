// 808: XFAIL - make_shared assignment to existing shared_ptr
// Assignment from make_shared to an existing shared_ptr doesn't propagate
// the _nonnull = 1 postcondition. DTT tracks the default constructor's
// _nonnull = 0 but doesn't update it through the assignment operator.

#include <memory>

void test_reset_make_shared() {
    std::shared_ptr<int> ptr;
    ptr = std::make_shared<int>(42);
    *ptr;  // Safe: make_shared always returns non-null (or throws)
}

int main() { return 0; }
