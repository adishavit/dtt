// 903_parenthesized_compound_fail.cpp
// Test: parenthesized compound conditions must be parsed correctly
//
// The old parser uses s.find("&&") which breaks on:
//   "(a && b) || c" -> splits at first && regardless of parens
//
// This test MUST fail (DTT error) when parser works correctly.
// If it compiles silently, the parser is broken.

struct SafeArray {
    int _size = 0;

    // Parenthesized compound: (bounds check) || (empty array is ok)
    // Old parser breaks this: finds first && and splits wrong
    [[clang::annotate("dtt:requires=(arg0 >= 0 && arg0 < _size) || _size == 0")]]
    int at(int idx) { return 0; }
};

// Variant 1: Negative index with non-empty array
void test_negative_index() {
    SafeArray arr;
    arr._size = 10;

    // idx = -1 violates: (arg0 >= 0 && arg0 < _size) is false (arg0 >= 0 fails)
    // _size == 0 is also false (10 != 0)
    // So the OR is false -> precondition violated -> DTT ERROR
    arr.at(-1);  // MUST produce DTT error  dtt-error {{\(arg0 >= 0 \&\& arg0 < _size\) \|\| _size == 0}}
}

// Variant 2: Out of bounds with non-empty array
void test_out_of_bounds() {
    SafeArray arr;
    arr._size = 5;

    // idx = 10 violates: (arg0 >= 0 && arg0 < _size) is false (arg0 < _size fails: 10 >= 5)
    // _size == 0 is also false (5 != 0)
    // So the OR is false -> precondition violated -> DTT ERROR
    arr.at(10);  // MUST produce DTT error  dtt-error {{\(arg0 >= 0 \&\& arg0 < _size\) \|\| _size == 0}}
}

// Variant 3: Valid access - should NOT produce error
void test_valid_access_pass() {
    SafeArray arr;
    arr._size = 10;
    arr.at(5);  // OK: 5 >= 0 && 5 < 10 is true
}

// Variant 4: Empty array access - edge case (might be OK depending on semantics)
void test_empty_array_pass() {
    SafeArray arr;
    arr._size = 0;
    arr.at(0);  // OK: _size == 0 is true (second disjunct)
}

int main() {
    test_negative_index();
    test_out_of_bounds();
    return 0;
}
