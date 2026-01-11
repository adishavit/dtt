// WHAT IS TESTED:
// For-loop reversed comparison should still catch violations.
//
// Expected: ERROR

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

// Wrong variable with reversed comparison
void test_wrong_variable() {
    Vec v;
    v._size = 3;
    int j = 10;

    for (int i = 0; v._size > i; ++i) {
        v[j] = 1;  // ERROR: j not guarded
    }
}

int main() { test_wrong_variable(); return 0; }
