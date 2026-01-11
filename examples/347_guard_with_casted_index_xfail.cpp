// WHAT IS TESTED:
// Guards with casted indices are not recognized as var-bound proofs.
//
// Expected behavior: PASS (casted i < _size should allow access)
// Current behavior: FAIL (guard not recognized)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test(long i) {
    Vec v;
    v._size = 3;

    if (static_cast<int>(i) < v._size) {
        v[static_cast<int>(i)] = 1;
    }
}

int main() { test(1); return 0; }
