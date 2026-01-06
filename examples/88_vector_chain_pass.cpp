// Example 88: Vector with chained operations — should COMPILE
// Tests tracking through push_back, pop_back, clear

#include "../include/dtt_vector.h"

void test_chain() {
    dtt::vector<int> v;
    // _size = 0
    
    v.push_back(1);  // _size = 1
    v.push_back(2);  // _size = 2
    v.push_back(3);  // _size = 3
    
    v.front();  // OK: _size > 0
    v.back();   // OK: _size > 0
    
    v.pop_back();  // _size = 2
    v.pop_back();  // _size = 1
    
    v.front();  // OK: _size > 0 still
    
    v.pop_back();  // _size = 0
    // v.front();  // Would fail: _size = 0
}

int main() {
    test_chain();
    return 0;
}
