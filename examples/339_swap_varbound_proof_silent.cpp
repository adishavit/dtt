// WHAT IS TESTED:
// VarBoundProofs are not invalidated when objects are swapped or reassigned.
//
// Expected behavior: ERROR (proof should not survive object mutation)
// Current behavior: SILENT (proof leaks through swap/reassign)
//
// Note: libc++ has different null tracking through pointer swaps that causes
// a false positive on pa nullability. Skip this test for libc++.

#include <utility>

#ifdef _LIBCPP_VERSION
int main() { return 0; }
#else

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test_swap_invalidates_proof(int i) {
    Vec a, b;
    a._size = 10;
    b._size = 0;

    if (i < a._size) {
        std::swap(a, b);
        a[i] = 1;  // Should error: after swap, a has b's old _size = 0
    }
}

void test_reassign_invalidates_proof(int i) {
    Vec a, b;
    a._size = 10;
    b._size = 0;

    if (i < a._size) {
        a = b;
        a[i] = 1;  // Should error: a now has _size = 0
    }
}

void test_move_invalidates_proof(int i) {
    Vec a, b;
    a._size = 10;
    b._size = 0;

    if (i < a._size) {
        a = std::move(b);
        a[i] = 1;  // Should error: a now has b's _size = 0
    }
}

void test_pointer_swap_invalidates_proof(int i) {
    Vec a, b;
    a._size = 10;
    b._size = 0;
    Vec* pa = &a;
    Vec* pb = &b;

    if (i < pa->_size) {
        std::swap(pa, pb);
        (*pa)[i] = 1;  // Should error: pa now points to b with _size = 0
    }
}

int main() {
    test_swap_invalidates_proof(5);
    test_reassign_invalidates_proof(5);
    test_move_invalidates_proof(5);
    test_pointer_swap_invalidates_proof(5);
    return 0;
}

#endif // !_LIBCPP_VERSION
