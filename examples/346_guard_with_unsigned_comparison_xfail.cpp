// WHAT IS TESTED:
// Guards with unsigned comparisons don't establish var-bound proofs.
//
// Expected behavior: PASS (i < _size proves bounds)
// Current behavior: FAIL (unsigned comparison ignored)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test(unsigned i) {
    Vec v;
    v._size = 3;

    if (i < static_cast<unsigned>(v._size)) {
        v[static_cast<int>(i)] = 1;
    }
}

int main() { test(1); return 0; }
