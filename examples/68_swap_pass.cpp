// Example 68: swap() property exchange — should COMPILE
// DTT tracks swap to exchange properties between objects

#include "../include/dtt_vector.h"
#include <utility>  // std::swap

void test_member_swap() {
    dtt::vector<int> v1, v2;
    v1.resize(3);  // v1._size = 3
    v2.resize(7);  // v2._size = 7

    v1.swap(v2);   // v1._size = 7, v2._size = 3

    v1[6];  // OK: 6 < 7 (v1 now has 7)
    v2[2];  // OK: 2 < 3 (v2 now has 3)
}

void test_std_swap() {
    dtt::vector<int> a, b;
    a.resize(5);   // a._size = 5
    b.resize(10);  // b._size = 10

    std::swap(a, b);  // a._size = 10, b._size = 5

    a[9];  // OK: 9 < 10 (a now has 10)
    b[4];  // OK: 4 < 5 (b now has 5)
}

void test_swap_preserves_access() {
    dtt::vector<int> small, large;
    small.resize(2);   // small._size = 2
    large.resize(100); // large._size = 100

    large.swap(small); // large._size = 2, small._size = 100

    small[99];  // OK: 99 < 100
    large[1];   // OK: 1 < 2
}

int main() {
    test_member_swap();
    test_std_swap();
    test_swap_preserves_access();
    return 0;
}
