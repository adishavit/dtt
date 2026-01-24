// Example 66: Copy/move assignment tracking — should COMPILE
// DTT tracks operator= to copy properties from RHS to LHS

#include <vector>

void test_copy_assignment() {
    std::vector<int> v1;
    v1.resize(5);  // v1._size = 5

    std::vector<int> v2;
    v2 = v1;  // v2._size = 5 (copied from v1)

    v2[0];  // OK: 0 < 5
    v2[4];  // OK: 4 < 5
    v2.front();  // OK: _size > 0
}

void test_move_assignment() {
    std::vector<int> v1;
    v1.resize(10);  // v1._size = 10

    std::vector<int> v2;
    v2 = std::move(v1);  // v2._size = 10 (moved from v1)

    v2[9];  // OK: 9 < 10
    v2.back();  // OK: _size > 0
}

void test_chain_assignment() {
    std::vector<int> v1, v2, v3;
    v1.resize(3);  // v1._size = 3

    v3 = v2 = v1;  // All get _size = 3

    v2[2];  // OK: 2 < 3
    v3[2];  // OK: 2 < 3
}

int main() {
    test_copy_assignment();
    test_move_assignment();
    test_chain_assignment();
    return 0;
}
