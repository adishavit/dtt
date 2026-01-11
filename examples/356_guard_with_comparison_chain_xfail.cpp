// WHAT IS TESTED:
// Chained comparisons aren't recognized (0 <= i && i < _size).
//
// Expected behavior: PASS (guard should prove i < _size)
// Current behavior: FAIL (no proof for chained guard)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test(int i) {
    Vec v;
    v._size = 3;

    if (0 <= i && i < v._size) {
        v[i] = 1;
    }
}

int main() { test(1); return 0; }
