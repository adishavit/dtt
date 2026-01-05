// Example 72: Self-assignment is a no-op — should COMPILE
// v = v should preserve properties

#include "../include/dtt_vector.h"

void test_self_copy_assign() {
    dtt::vector<int> v;
    v.resize(5);  // v._size = 5

    v = v;  // Self-assignment — no-op

    v[4];  // OK: 4 < 5 (properties preserved)
    v.front();  // OK: _size > 0
}

void test_self_after_operations() {
    dtt::vector<int> v;
    v.resize(10);
    v.push_back(42);  // v._size = 11

    v = v;  // Self-assignment

    v[10];  // OK: 10 < 11
}

int main() {
    test_self_copy_assign();
    test_self_after_operations();
    return 0;
}
