// WHAT IS TESTED:
// Auto-postcondition inference: DTT should infer postconditions from method bodies.
// This test: _size = 0 (constant assignment) should infer ensures=_size == 0
//
// EXPECTED: DTT infers postcondition from clear() implementation, detects that
// v._size becomes 0, and catches the subsequent unsafe access.
// CURRENTLY: Gap - DTT doesn't infer postconditions from implementations yet.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    // NO annotation - DTT should infer ensures=_size == 0 from implementation
    void clear() {
        _size = 0;  // <-- DTT should detect this sets _size to 0
    }
};

void test_auto_postcond_constant() {
    Vec v;
    v._size = 5;
    v.clear();    // After this, _size should be inferred as 0
    v.front();    // BUG: _size == 0, violates requires=_size > 0
}

int main() {
    test_auto_postcond_constant();
    return 0;
}
