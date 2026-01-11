// WHAT IS TESTED:
// Guards with constexpr bounds on _size are not recognized for proofs.
//
// Expected behavior: PASS (i < kSize implies i < _size when _size==kSize)
// Current behavior: FAIL (no proof)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

constexpr int kSize = 3;

void test(int i) {
    Vec v;
    v._size = kSize;

    if (i < kSize) {
        v[i] = 1;
    }
}

int main() { test(1); return 0; }
