// Example 64: std::vector assign() method — should COMPILE

#include <vector>

void test_assign_count() {
    std::vector<int> v;
    v.assign(5, 42);  // _size = 5 (via ensures=_size == arg0)

    v[0];  // OK: 0 < 5
    v[4];  // OK: 4 < 5
    v.front();  // OK: _size > 0
}

void test_assign_after_clear() {
    std::vector<int> v;
    v.resize(10);
    v.clear();      // _size = 0
    v.assign(3, 1); // _size = 3

    v[0];  // OK: 0 < 3
    v[2];  // OK: 2 < 3
}

int main() {
    test_assign_count();
    test_assign_after_clear();
    return 0;
}
