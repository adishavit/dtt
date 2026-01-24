// Example 74: Complex patterns with OOB — should FAIL
// Tests that DTT correctly tracks through swap/assign chains

#include <vector>
#include <utility>

void test_swap_then_oob() {
    std::vector<int> a, b;
    a.resize(3);   // a._size = 3
    b.resize(10);  // b._size = 10

    a.swap(b);     // a._size = 10, b._size = 3

    b[5];  // ERROR: 5 >= 3 (b now has only 3)
}

int main() {
    test_swap_then_oob();
    return 0;
}
