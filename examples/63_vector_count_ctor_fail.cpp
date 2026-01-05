// Example 63: dtt::vector count constructor bounds check — should FAIL

#include "../include/dtt_vector.h"

void test_out_of_bounds() {
    dtt::vector<int> v(5);  // _size = 5

    v[5];   // ERROR: 5 < 5 is false
}

int main() {
    test_out_of_bounds();
    return 0;
}
