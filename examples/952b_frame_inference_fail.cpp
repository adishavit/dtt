// 952b_frame_inference_fail.cpp
// Tests: Frame inference - proof invalidated when function modifies the field

struct Vec {
    int* _data;
    int _size;
    int _capacity;
};

[[clang::annotate("dtt:requires=_size > 0")]]
int front(Vec& v) { return v._data[0]; }

void clear(Vec& v) {
    v._size = 0;
}

// Proof should NOT survive call that touches it
// Expected: FAIL - v._size proof is invalidated by clear()
void test_frame_invalidated() {
    Vec v;
    v._size = 5;           // Proof: v._size > 0
    clear(v);              // Invalidates _size proof
    front(v);              // Should FAIL: v._size is now 0  dtt-error {{_size > 0}}
}

int main() {
    test_frame_invalidated();
    return 0;
}
