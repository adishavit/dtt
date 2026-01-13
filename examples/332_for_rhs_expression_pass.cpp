// Tests: For-loop RHS arithmetic expression establishes proof
// Issue #25: FIXED - i < v._size - N now establishes bounds proof

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test() {
    Vec v;
    v._size = 4;

    for (int i = 0; i < v._size - 1; ++i) {
        v[i] = 1;
    }
}

int main() { test(); return 0; }
