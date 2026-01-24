// Example 65: std::vector assign bounds check — should FAIL

#include <vector>

void test_assign_out_of_bounds() {
    std::vector<int> v;
    v.assign(5, 42);  // _size = 5

    v[5];  // ERROR: 5 < 5 is false
}

int main() {
    test_assign_out_of_bounds();
    return 0;
}
