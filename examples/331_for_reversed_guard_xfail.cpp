// WHAT IS TESTED:
// For-loop bounds aren't recognized when the comparison is reversed.
//
// Expected behavior: PASS (guard should prove i < _size)
// Current behavior: FAIL (no proof for reversed comparison)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test() {
    Vec v;
    v._size = 3;

    for (int i = 0; v._size > i; ++i) {
        v[i] = 1;
    }
}

int main() { test(); return 0; }
