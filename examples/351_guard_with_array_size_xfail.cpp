// WHAT IS TESTED:
// Guards on array size fields don't propagate to element access.
//
// Expected behavior: PASS (arr._size guard should allow access)
// Current behavior: FAIL (no proof for guard on array element)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test(int i) {
    Vec arr[2];
    arr[0]._size = 3;

    if (i < arr[0]._size) {
        arr[0][i] = 1;
    }
}

int main() { test(1); return 0; }
