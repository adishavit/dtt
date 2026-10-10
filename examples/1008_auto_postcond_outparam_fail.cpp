// WHAT IS TESTED:
// Auto-postcondition inference for out-parameter (reference) modifications.
// This test verifies DTT catches bugs when clear() is called via free function.
//
// EXPECTED: DTT infers postcondition from clearVec() implementation, tracks that
// v._size becomes 0, and catches the subsequent unsafe access.
// RESULT: DTT correctly catches the bug.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

// Free function that clears
// NO annotation - DTT should infer ensures=arg0._size == 0
void clearVec(Vec& v) {
    v._size = 0;
}

void test_outparam_postcond_catches_bug() {
    Vec v;
    v._size = 5;
    clearVec(v);  // After this, v._size should be inferred as 0
    v.front();    // BUG: _size == 0, violates requires=_size > 0  dtt-error {{_size > 0}}
}

int main() {
    test_outparam_postcond_catches_bug();
    return 0;
}
