// WHAT IS TESTED:
// Parenthesized constraints are now parsed correctly and enforced.
//
// Expected: ERROR (constraints should be enforced)
// Previously: SILENT (constraints were ignored due to parentheses bug)

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=(_size > 0)")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:requires=(arg0 < _size)")]]
    int& at(int idx) { return data[idx]; }

    [[clang::annotate("dtt:requires=(((_size > 0)))")]]
    int& back() { return data[_size - 1]; }
};

void test_single_parens() {
    Vec v;
    v._size = 0;
    v.front();  // Should error: _size = 0 violates (_size > 0)
}

void test_arg_constraint_parens() {
    Vec v;
    v._size = 3;
    v.at(5);  // Should error: 5 >= 3 violates (arg0 < _size)
}

void test_nested_parens() {
    Vec v;
    v._size = 0;
    v.back();  // Should error: _size = 0 violates (((_size > 0)))
}

int main() {
    test_single_parens();
    test_arg_constraint_parens();
    test_nested_parens();
    return 0;
}
