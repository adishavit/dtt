// Example 94: Initialize from function return — should COMPILE
// Tests postcondition application on variable initialization

#include <vector>

std::vector<int> makeVector(int n) {
    std::vector<int> v;
    v.resize(n);
    return v;
}

void test() {
    std::vector<int> v;
    v.resize(10);
    
    v[0];  // OK: 0 < 10
    v[9];  // OK: 9 < 10
}

int main() {
    test();
    return 0;
}
