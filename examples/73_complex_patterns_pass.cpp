// Example 73: Complex assignment and swap patterns — should COMPILE
// Tests various combinations of swap and assignment

#include <vector>
#include <utility>

void test_swap_then_assign() {
    std::vector<int> a, b, c;
    a.resize(5);   // a._size = 5
    b.resize(10);  // b._size = 10
    c.resize(3);   // c._size = 3

    a.swap(b);     // a._size = 10, b._size = 5
    c = a;         // c._size = 10

    c[9];  // OK: 9 < 10
    a[9];  // OK: 9 < 10
    b[4];  // OK: 4 < 5
}

void test_assign_then_swap() {
    std::vector<int> x, y, z;
    x.resize(8);   // x._size = 8
    y.resize(2);   // y._size = 2

    z = x;         // z._size = 8
    z.swap(y);     // z._size = 2, y._size = 8

    y[7];  // OK: 7 < 8
    z[1];  // OK: 1 < 2
}

void test_multiple_swaps() {
    std::vector<int> p, q, r;
    p.resize(4);   // p._size = 4
    q.resize(6);   // q._size = 6
    r.resize(8);   // r._size = 8

    p.swap(q);     // p._size = 6, q._size = 4
    q.swap(r);     // q._size = 8, r._size = 4

    p[5];  // OK: 5 < 6
    q[7];  // OK: 7 < 8
    r[3];  // OK: 3 < 4
}

void test_std_swap_chain() {
    std::vector<int> a, b;
    a.resize(3);
    b.resize(7);

    std::swap(a, b);  // a._size = 7, b._size = 3

    a[6];  // OK: 6 < 7
    b[2];  // OK: 2 < 3
}

int main() {
    test_swap_then_assign();
    test_assign_then_swap();
    test_multiple_swaps();
    test_std_swap_chain();
    return 0;
}
