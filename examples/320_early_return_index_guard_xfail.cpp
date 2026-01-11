// WHAT IS TESTED:
// Early-return guards do not create proofs after the guard.
//
// Expected behavior: PASS (if i >= _size return; implies i < _size)
// Current behavior: FAIL (no proof after early return)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test(int i) {
    Vec v;
    v._size = 3;

    if (i >= v._size) {
        return;
    }

    v[i] = 1;
}

int main() { test(1); return 0; }
