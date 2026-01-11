// WHAT IS TESTED:
// Guards using function calls (abs) on index are not recognized.
//
// Expected behavior: PASS (abs(i) < _size should allow access)
// Current behavior: FAIL (guard not recognized)

int abs(int);

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test(int i) {
    Vec v;
    v._size = 3;

    if (abs(i) < v._size) {
        v[abs(i)] = 1;
    }
}

int main() { test(1); return 0; }
