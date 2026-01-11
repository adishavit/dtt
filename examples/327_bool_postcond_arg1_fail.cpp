// WHAT IS TESTED:
// Boolean postconditions should catch violations outside guard.
//
// Expected: ERROR

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

[[clang::annotate("dtt:ensures_if_true=arg1._size > 0")]]
bool isNonEmpty(int dummy, const Vec& v) {
    (void)dummy;
    return v._size > 0;
}

// Access without guard
void test_no_guard() {
    Vec v;
    v._size = 0;
    v.front();  // ERROR: _size = 0, not > 0
}

int main() { test_no_guard(); return 0; }
