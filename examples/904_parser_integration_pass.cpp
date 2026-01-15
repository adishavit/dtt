// 904_parser_integration_pass.cpp
// Test: ConstraintExprParser integration - complex constraint patterns
//
// These patterns exercise the full parser capabilities:
// - Parenthesized expressions
// - old() references
// - Arithmetic in constraints
// - Multiple arguments
// - Nested logical operators

struct Container {
    int _size = 0;
    int _capacity = 0;
    int* data = nullptr;

    // Pattern 1: old() in postcondition
    [[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    void push_back(int x) {
        if (_size < _capacity) {
            data[_size++] = x;
        }
    }

    // Pattern 2: Arithmetic constraint with parens
    [[clang::annotate("dtt:requires=(arg0 + arg1) <= _size")]]
    void copy_range(int start, int count) {
        (void)start; (void)count;
    }

    // Pattern 3: Complex postcondition
    [[clang::annotate("dtt:ensures=_size == 0 && _capacity == old(_capacity)")]]
    void clear() {
        _size = 0;
    }

    // Pattern 4: Nested parens in compound
    [[clang::annotate("dtt:requires=((arg0 >= 0) && (arg0 < _size))")]]
    int& at(int idx) { return data[idx]; }
};

// Test that valid operations don't trigger errors
void test_valid_operations() {
    Container c;
    c._size = 5;
    c._capacity = 10;
    c.data = new int[10];

    // Valid: 0 + 3 = 3 <= 5
    c.copy_range(0, 3);

    // Valid: 2 >= 0 && 2 < 5
    c.at(2);

    delete[] c.data;
}

int main() {
    test_valid_operations();
    return 0;
}
