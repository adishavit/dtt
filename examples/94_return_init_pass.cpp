// Example 94: Initialize from function return — should COMPILE
// Tests postcondition application on variable initialization

#include "../include/dtt_vector.h"

dtt::vector<int> makeVector(int n) {
    dtt::vector<int> v;
    v.resize(n);
    return v;
}

void test() {
    dtt::vector<int> v;
    v.resize(10);
    
    v[0];  // OK: 0 < 10
    v[9];  // OK: 9 < 10
}

int main() {
    test();
    return 0;
}
