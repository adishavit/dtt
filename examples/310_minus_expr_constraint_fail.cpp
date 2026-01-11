// WHAT IS TESTED:
// Expression constraints with arithmetic should catch violations.
//
// Expected: ERROR (constraints violated)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 - 1 < _size")]]
    int& at_minus_one(int idx) { return *reinterpret_cast<int*>(0); }
};

void test_minus_violation() {
    Vec v;
    v._size = 3;
    v.at_minus_one(5);  // 5 - 1 = 4 >= 3, ERROR
}

int main() { test_minus_violation(); return 0; }
