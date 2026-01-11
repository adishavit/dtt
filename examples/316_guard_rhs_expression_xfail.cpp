// WHAT IS TESTED:
// Var-bound guards don't handle expressions on the RHS (v._size - 1).
//
// Expected behavior: PASS (i < _size - 1 implies i < _size)
// Current behavior: FAIL (no proof created)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test(int i) {
    Vec v;
    v._size = 4;

    if (i < v._size - 1) {
        v[i] = 1;
    }
}

int main() { test(1); return 0; }
