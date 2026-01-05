// Example 58: Index arithmetic failure — should NOT compile
// Tests: computed index that exceeds bounds

#include "../include/dtt_vector.h"

void test_bad_index() {
    dtt::vector<int> v;
    v.resize(10);  // _size = 10
    
    int idx = 15;   // Out of bounds!
    v[idx];         // ERROR: 15 >= 10
}

int main() {
    test_bad_index();
    return 0;
}
