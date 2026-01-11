// WHAT IS TESTED:
// Guards on reference parameters are not tracked for var-bound proofs.
//
// Expected behavior: PASS (guard proves ref < _size)
// Current behavior: FAIL (no proof for ref)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test(int& i) {
    Vec v;
    v._size = 3;

    if (i < v._size) {
        v[i] = 1;
    }
}

int main() {
    int i = 1;
    test(i);
    return 0;
}
