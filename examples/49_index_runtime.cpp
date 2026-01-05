// Example 49: Runtime index — should COMPILE (can't check at compile time)

#include "../include/dtt_vector.h"

int main() {
    dtt::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    
    int i = 0;
    v[i] = 42;  // Runtime index - no compile-time check possible
    
    return 0;
}
