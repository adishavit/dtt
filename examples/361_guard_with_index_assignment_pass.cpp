// WHAT IS TESTED:
// Variable assignment inside guard scope should inherit proofs.
// When j = i and i has proof i < v._size, j should inherit that proof.
// Chained assignments should also propagate proofs.
//
// Expected: PASS (all assignments inherit proofs correctly)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test_basic_assignment(int i) {
    Vec v;
    v._size = 3;

    if (i < v._size) {
        int j = i;
        v[j] = 1;  // Safe: j inherits proof from i
    }
}

void test_chained_assignment(int i) {
    Vec v;
    v._size = 3;

    if (i < v._size) {
        int j = i;
        int k = j;
        v[k] = 1;  // Safe: k inherits proof through j from i
    }
}

int main() {
    test_basic_assignment(1);
    test_chained_assignment(1);
    return 0;
}
