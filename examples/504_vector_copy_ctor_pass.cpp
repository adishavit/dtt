// Example 504: dtt::vector copy/move constructor tracking — should COMPILE
// Size should transfer through copy and move construction

#include "../include/dtt_vector.h"

void test_copy_ctor() {
    dtt::vector<int> v1;
    v1.resize(5);  // v1._size = 5

    dtt::vector<int> v2(v1);  // Copy: v2._size should be 5
    v2[4];  // OK if copy tracked: 4 < 5
}

void test_move_ctor() {
    dtt::vector<int> v1;
    v1.resize(10);  // v1._size = 10

    dtt::vector<int> v2(std::move(v1));  // Move: v2._size should be 10
    v2[9];  // OK if move tracked: 9 < 10
}

void test_copy_from_sized() {
    dtt::vector<int> v1(7);  // v1._size = 7
    dtt::vector<int> v2(v1);  // Copy: v2._size = 7
    v2[6];  // OK: 6 < 7
}

int main() {
    test_copy_ctor();
    test_move_ctor();
    test_copy_from_sized();
    return 0;
}
