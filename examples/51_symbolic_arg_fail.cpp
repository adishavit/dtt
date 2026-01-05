// Example 51: Symbolic argument tracking fail — should FAIL

#include "../include/dtt_vector.h"

int main() {
    dtt::vector<int> v;
    v.resize(3);   // _size = 3
    v[3] = 42;     // ERROR: 3 < 3 is false
    return 0;
}
