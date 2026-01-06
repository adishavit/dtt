// Example 91: Postcondition decrement tracking — should COMPILE
// Tests old(_size) - 1 postcondition

#include "../include/dtt_vector.h"

void test() {
    dtt::vector<int> v;
    v.resize(5);  // _size = 5
    
    v.pop_back();  // _size = old(5) - 1 = 4
    v.pop_back();  // _size = old(4) - 1 = 3
    v.pop_back();  // _size = old(3) - 1 = 2
    
    v[0];  // OK: 0 < 2
    v[1];  // OK: 1 < 2
}

int main() {
    test();
    return 0;
}
