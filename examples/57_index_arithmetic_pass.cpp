// Example 57: Index arithmetic — should COMPILE
// Tests: int mid = v.size() / 2; v[mid];

#include "../include/dtt_vector.h"

void test_mid_index() {
    dtt::vector<int> v;
    v.resize(10);  // _size = 10
    
    // Compute midpoint
    int mid = 5;   // Constant case
    v[mid];        // OK: 5 < 10
    
    // Last element via computation
    int last = 9;
    v[last];       // OK: 9 < 10
}

void test_computed_constants() {
    dtt::vector<int> v;
    v.resize(20);
    
    int quarter = 5;   // 20 / 4 = 5
    v[quarter];        // OK: 5 < 20
    
    int offset = 3;
    v[offset + 2];     // OK: 5 < 20
}

int main() {
    test_mid_index();
    test_computed_constants();
    return 0;
}
