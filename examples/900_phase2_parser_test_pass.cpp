// DTT Phase 2 Parser Validation Test
// Tests that the new ConstraintExprParser produces correct AST
// This file should compile without DTT errors

#include "../include/dtt_vector.h"

// Test basic constraint parsing via annotations
// If parsing fails, DTT would not recognize the constraints

struct TestContainer {
    int _size = 0;
    int _capacity = 10;

    // Simple field comparison
    [[clang::annotate("dtt:requires=_size > 0")]]
    int front() { return 0; }

    // Field with equals
    [[clang::annotate("dtt:ensures=_size == 0")]]
    void clear() { _size = 0; }

    // Compound constraint (AND)
    [[clang::annotate("dtt:requires=_size > 0 && _size < _capacity")]]
    void checkBounds() {}

    // Arithmetic in constraint
    [[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    void push() { _size++; }

    // Argument constraint
    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int at(int idx) { return 0; }

    // Multiple argument constraint
    [[clang::annotate("dtt:requires=arg0 + arg1 <= _size")]]
    void range(int start, int len) {}
};

int main() {
    TestContainer c;
    c._size = 5;
    c._capacity = 10;

    // Test requires=_size > 0
    c.front();  // OK: 5 > 0

    // Test requires=arg0 < _size
    c.at(2);    // OK: 2 < 5

    // Test requires=arg0 + arg1 <= _size
    c.range(1, 3);  // OK: 1 + 3 = 4 <= 5

    // Test ensures=_size == old(_size) + 1
    c.push();   // _size becomes 6

    // Test compound constraint
    c.checkBounds();  // OK: 6 > 0 && 6 < 10

    return 0;
}
