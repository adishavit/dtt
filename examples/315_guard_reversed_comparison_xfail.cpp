// WHAT IS TESTED:
// Var-bound guards only recognize "i < v._size", not "v._size > i".
//
// Expected behavior: PASS (guard should prove i < _size)
// Current behavior: FAIL (no proof created)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test(int i) {
    Vec v;
    v._size = 3;

    if (v._size > i) {
        v[i] = 1;
    }
}

int main() { test(1); return 0; }
