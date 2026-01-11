// WHAT IS TESTED:
// do-while guards do not create var-bound proofs.
//
// Expected behavior: PASS (guard should prove i < _size)
// Current behavior: FAIL (no proof for do-while)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test() {
    Vec v;
    v._size = 3;
    int i = 0;

    do {
        v[i] = 1;
        ++i;
    } while (i < v._size);
}

int main() { test(); return 0; }
