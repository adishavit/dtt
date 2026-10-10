// WHAT IS TESTED:
// Assignment outside guard scope should not inherit non-existent proofs.
//
// Expected: ERROR (no proof that j < v._size)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test(int i) {
    Vec v;
    v._size = 3;

    int j = i;  // No guard - j has no proof
    v[j] = 1;   // ERROR: j could be >= v._size  dtt-error {{i < 3}}
}

int main() { test(1); return 0; }
