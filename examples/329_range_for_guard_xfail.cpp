// WHAT IS TESTED:
// Range-based for loops do not establish index bounds proofs.
//
// Expected behavior: PASS (loop implies i < _size)
// Current behavior: FAIL (no proof from range-for)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test() {
    Vec v;
    v._size = 3;
    int i = 0;

    for (int value : {1, 2, 3}) {
        (void)value;
        v[i] = 1;
        ++i;
    }
}

int main() { test(); return 0; }
