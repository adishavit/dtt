// WHAT IS TESTED:
// Guards on array subscript base paths with runtime indices are ignored.
//
// Expected behavior: PASS (guard should prove element is safe)
// Current behavior: FAIL (runtime index causes guard to be dropped)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& at(int idx) { return *reinterpret_cast<int*>(0); }
};

void test(int i) {
    Vec arr[2];
    arr[0]._size = 3;

    if (i < arr[0]._size) {
        arr[0].at(i);
    }
}

int main() { test(1); return 0; }
