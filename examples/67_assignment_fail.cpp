// Example 67: Copy assignment — should FAIL
// After assignment, LHS gets RHS properties - OOB access should fail

#include <vector>

void test_oob_after_assignment() {
    std::vector<int> v1;
    v1.resize(2);  // v1._size = 2

    std::vector<int> v2;
    v2.resize(10);  // v2._size = 10 (larger)

    v2 = v1;  // v2._size = 2 (from v1)

    v2[5];  // ERROR: 5 >= 2
}

int main() {
    test_oob_after_assignment();
    return 0;
}
