// WHAT IS TESTED:
// VarBoundProofs are not invalidated when objects are swapped.
//
// Expected behavior: ERROR (proof should not survive swap)
// Current behavior: SILENT (proof leaks through swap)

#include <utility>

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test(int i) {
    Vec a;
    Vec b;
    a._size = 1;
    b._size = 0;

    if (i < a._size) {
        std::swap(a, b);
        a[i] = 1; // Should be unsafe if b._size was 0
    }
}

int main() { test(0); return 0; }
