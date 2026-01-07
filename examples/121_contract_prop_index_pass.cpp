// Example 121: Contract propagation — index bounds constraint — should COMPILE
// arg0 < _size becomes a VarBoundProof inside the function

#include "../include/dtt_vector.h"

// Free function that requires index < size
[[clang::annotate("dtt:requires=arg0 < _size")]]
int getAt(size_t idx, dtt::vector<int>& v) {
    // Inside this function, we have proof that idx < v._size
    return v[idx];  // Should PASS: idx < _size is assumed
}

int main() { return 0; }
