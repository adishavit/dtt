// Example 502: Early return proof propagation — should FAIL
// The negation proof should not allow incorrect access

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:requires=_size > 1")]]
    int& second() { return data[1]; }
};

// Early return only proves > 0, not > 1
void test_insufficient_proof(Vec& v) {
    if (v._size == 0) {
        return;
    }
    // Here: _size != 0, so _size >= 1
    v.front();   // OK: _size > 0
    v.second();  // FAIL: _size > 1 not proven (could be exactly 1)  dtt-error {{_size > 1}}
}

int main() {
    Vec v;
    v._size = 1;
    test_insufficient_proof(v);
    return 0;
}
