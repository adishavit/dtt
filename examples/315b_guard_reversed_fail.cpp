// WHAT IS TESTED:
// Without the guard, reversed comparison awareness shouldn't cause false negatives.
//
// Expected: ERROR (no guard, i could be >= _size)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test(int i) {
    Vec v;
    v._size = 3;

    // No guard - should fail
    v[i] = 1;
}

int main() { test(1); return 0; }
