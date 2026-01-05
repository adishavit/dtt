// Example 71: Move assignment — destination gets source size — should FAIL
// After move, destination has source's properties, OOB should fail

#include "../include/dtt_vector.h"
#include <utility>

void test_move_oob() {
    dtt::vector<int> v1;
    v1.resize(3);  // v1._size = 3

    dtt::vector<int> v2;
    v2.resize(10);  // v2._size = 10 (larger)

    v2 = std::move(v1);  // v2._size = 3 (from v1)

    v2[5];  // ERROR: 5 >= 3
}

int main() {
    test_move_oob();
    return 0;
}
