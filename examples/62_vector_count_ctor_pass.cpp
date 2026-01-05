// Example 62: dtt::vector count constructor tracks size — should COMPILE

#include "../include/dtt_vector.h"

void test_count_ctor() {
    dtt::vector<int> v(10);  // _size = 10 (via ensures=_size == arg0)

    v[0];   // OK: 0 < 10
    v[9];   // OK: 9 < 10
    v.front();  // OK: _size > 0
    v.back();   // OK: _size > 0
}

void test_count_value_ctor() {
    dtt::vector<int> v(5, 42);  // _size = 5

    v[0];   // OK: 0 < 5
    v[4];   // OK: 4 < 5
}

int main() {
    test_count_ctor();
    test_count_value_ctor();
    return 0;
}
