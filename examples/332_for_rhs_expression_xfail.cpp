// WHAT IS TESTED:
// For-loop bounds don't handle expression RHS (v._size - 1).
//
// Expected behavior: PASS (i < _size - 1 implies i < _size)
// Current behavior: FAIL (no proof from loop condition)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test() {
    Vec v;
    v._size = 4;

    for (int i = 0; i < v._size - 1; ++i) {
        v[i] = 1;
    }
}

int main() { test(); return 0; }
