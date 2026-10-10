// WHAT IS TESTED:
// Auto-postcondition inference: DTT infers postconditions from method bodies.
// This test: _size = n (argument assignment) infers ensures=_size == arg0
//
// EXPECTED: DTT infers postcondition from resize() implementation, tracks that
// v._size becomes the passed argument, and catches the subsequent unsafe access.
// RESULT: DTT correctly catches the bug.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 4")]]
    int& at4() { return data[4]; }

    // NO annotation - DTT should infer ensures=_size == arg0 from implementation
    void resize(int n) {
        _size = n;  // <-- DTT should detect this sets _size to n (arg0)
    }
};

void test_auto_postcond_arg() {
    Vec v;
    v._size = 10;
    v.resize(3);  // After this, _size should be inferred as 3
    v.at4();      // BUG: _size == 3, violates requires=_size > 4  dtt-error {{_size > 4}}
}

int main() {
    test_auto_postcond_arg();
    return 0;
}
