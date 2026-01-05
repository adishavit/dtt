// Example 48: Index out of bounds — should FAIL

#include "../include/dtt_vector.h"

int main() {
    dtt::vector<int> v;
    
    v.push_back(1);  // _size = 1
    v.push_back(2);  // _size = 2
    
    v[0] = 10;  // OK: 0 < 2
    v[1] = 20;  // OK: 1 < 2
    v[2] = 30;  // ERROR: 2 < 2 is false
    
    return 0;
}
