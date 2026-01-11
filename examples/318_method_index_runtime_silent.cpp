// WHAT IS TESTED:
// Method arg0 < _size checks skip runtime index arguments.
//
// Expected behavior: ERROR (i may be out of bounds)
// Current behavior: SILENT (runtime arg not checked)

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& at(int idx) { return data[idx]; }
};

void test(int i) {
    Vec v;
    v._size = 0;
    v.at(i);
}

int main() { test(1); return 0; }
