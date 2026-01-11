// WHAT IS TESTED:
// Reference parameter access without guard should fail.
//
// Expected: ERROR

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

// Access reference param without guard
void test_no_guard(int& i) {
    Vec v;
    v._size = 3;
    v[i] = 1;  // ERROR: no guard on i
}

int main() {
    int x = 1;
    test_no_guard(x);
    return 0;
}
