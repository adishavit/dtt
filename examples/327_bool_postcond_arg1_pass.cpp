// WHAT IS TESTED:
// Boolean postconditions on arg1 (second argument) should work in guards.
//
// Expected: PASS

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

// Postcondition on arg1
[[clang::annotate("dtt:ensures_if_true=arg1._size > 0")]]
bool isNonEmpty(int dummy, const Vec& v) {
    (void)dummy;
    return v._size > 0;
}

// Basic arg1 postcondition
void test_basic() {
    int arr[1] = {};
    Vec v;
    v.data = arr;
    v._size = 1;
    if (isNonEmpty(0, v)) {
        v.front();  // v._size > 0 proven by postcondition
    }
}

// Multiple calls with arg1
void test_multiple() {
    int arr1[1] = {};
    int arr2[2] = {};
    Vec v1, v2;
    v1.data = arr1;
    v1._size = 1;
    v2.data = arr2;
    v2._size = 2;

    if (isNonEmpty(0, v1)) {
        v1.front();
    }
    if (isNonEmpty(0, v2)) {
        v2.front();
    }
}

// Arg1 with different dummy values
void test_different_dummy() {
    int arr[3] = {};
    Vec v;
    v.data = arr;
    v._size = 3;

    if (isNonEmpty(42, v)) {
        v.front();
    }
    if (isNonEmpty(-1, v)) {
        v.front();
    }
}

int main() {
    test_basic();
    test_multiple();
    test_different_dummy();
    return 0;
}
