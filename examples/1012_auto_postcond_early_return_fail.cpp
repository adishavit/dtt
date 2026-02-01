// WHAT IS TESTED:
// Auto-postcondition inference with early returns.
//
// EXPECTED: DTT tracks that pop_safe() decrements _size.
// RESULT: DTT correctly catches the bug.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    bool empty() const { return _size == 0; }

    // Early return guard - DTT should infer:
    // ensures = (old(_size) == 0 && _size == old(_size)) ||
    //           (old(_size) > 0 && _size == old(_size) - 1)
    void pop_safe() {
        if (empty()) return;
        _size--;
    }
};

// This should be caught: pop_safe when _size=1 results in _size=0
void test_early_return_bug() {
    Vec v;
    v._size = 1;
    v.pop_safe();  // _size = 0 after this
    v.front();     // BUG: _size == 0, violates requires > 0
}

int main() {
    test_early_return_bug();
    return 0;
}
