// Example 46: dtt::vector pop_back on empty — should FAIL

#include "../include/dtt_vector.h"

int main() {
    dtt::vector<int> v;
    
    v.pop_back();  // ERROR: _size = 0, pop_back requires _size > 0
    
    return 0;
}
