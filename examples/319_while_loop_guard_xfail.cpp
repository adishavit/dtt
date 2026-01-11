// WHAT IS TESTED:
// While-loop bounds are not tracked for var-bound proofs.
//
// Expected behavior: PASS (guard should prove i < _size)
// Current behavior: FAIL (no proof for while)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test() {
    Vec v;
    v._size = 3;
    int i = 0;

    while (i < v._size) {
        v[i] = 1;
        ++i;
    }
}

int main() { test(); return 0; }
