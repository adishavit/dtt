// Example 69: swap() OOB after exchange — should FAIL
// After swap, accessing old size should fail

#include <vector>
#include <utility>

void test_swap_oob() {
    std::vector<int> small, large;
    small.resize(2);   // small._size = 2
    large.resize(10);  // large._size = 10

    small.swap(large); // small._size = 10, large._size = 2

    large[5];  // ERROR: 5 >= 2 (large now only has 2)  dtt-error {{out of bounds}}
}

int main() {
    test_swap_oob();
    return 0;
}
