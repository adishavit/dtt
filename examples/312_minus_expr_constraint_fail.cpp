// Example 312: Subtraction in constraints — should FAIL
// Expression constraints with subtraction detect violations.

struct Vec {
    int _size;
    int data[10];

    [[clang::annotate("dtt:requires=arg0 - 1 < _size")]]
    int& at(int idx) { return data[idx]; }
};

void test() {
    Vec v;
    v._size = 3;

    // 4 - 1 < 3 is false (3 < 3), should fail
    v.at(4);  // FAIL: constraint violated
}

int main() { test(); return 0; }
