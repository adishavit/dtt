// Example 50: Symbolic argument tracking — should COMPILE

#include <vector>

void test_resize_const() {
    std::vector<int> v;
    v.resize(5);       // _size = 5
    v[0] = 1;          // OK: 0 < 5
    v[4] = 5;          // OK: 4 < 5
    v.front();         // OK: _size = 5 > 0
}

void test_assign_const() {
    std::vector<int> v;
    v.assign(3, 42);   // _size = 3
    v[0] = 1;          // OK: 0 < 3
    v[2] = 3;          // OK: 2 < 3
}

void test_insert_count() {
    std::vector<int> v;
    v.push_back(1);              // _size = 1
    v.insert(v.begin(), 2, 99);  // _size = 1 + 2 = 3
    v[0] = 1;                    // OK: 0 < 3
    v[1] = 2;                    // OK: 1 < 3
    v[2] = 3;                    // OK: 2 < 3
}

int main() {
    test_resize_const();
    test_assign_const();
    test_insert_count();
    return 0;
}
