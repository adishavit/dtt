// WHAT IS TESTED:
// Field expression constraints should catch violations.
//
// Expected: ERROR

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    void requires_positive() const {}
};

// Zero size violates _size > 0
void test_violation() {
    Vec v;
    v._size = 0;
    v.requires_positive();  // ERROR: 0 > 0 is false
}

int main() { test_violation(); return 0; }
