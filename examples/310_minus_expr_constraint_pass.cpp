// Example 310: Subtraction in constraints — should COMPILE
// Expression constraints with subtraction are correctly parsed.

struct Vec {
    int _size;
    int data[10];

    [[clang::annotate("dtt:requires=arg0 - 1 < _size")]]
    int& at(int idx) { return data[idx]; }
};

void test() {
    Vec v;
    v._size = 3;

    // 3 - 1 < 3 is true (2 < 3), should pass
    v.at(3);

    // Also test at boundary: 1 - 1 < 3 is true (0 < 3)
    v.at(1);
}

int main() { test(); return 0; }
