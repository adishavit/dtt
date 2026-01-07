// XFAIL: Expression constraint propagation — should COMPILE but currently FAILS
//
// WHAT IS TESTED:
// When a function has a precondition with arithmetic expressions
// (e.g., arg0 + arg1 < _size), that should be usable inside the body.
//
// WHY IT'S DIFFICULT:
// 1. Need symbolic expression representation in proofs
// 2. Must substitute actual parameter names for arg0, arg1, etc.
// 3. Arithmetic simplification: (a + b < n) doesn't directly prove (a < n)
// 4. Requires SMT-level reasoning for non-trivial expressions
//
// RATIONALE:
// Complex bounds checking often involves expressions:
// - memcpy(dst + offset, src, n): offset + n <= dst_size
// - substring(str, start, len): start + len <= str.size()
// - matrix access m[i][j]: i * cols + j < total_size
//
// CURRENT BEHAVIOR:
// Expression constraints are checked at call sites using Z3, but
// not propagated into function bodies because representing
// "arg0 + arg1 < _size" as an assumption requires expression tracking.

struct Buffer {
    char* data;
    int _size;
};

// Requires: combined offset and length fit in buffer
[[clang::annotate("dtt:requires=arg0 + arg1 < _size")]]
void writeAt(int offset, int length, Buffer& buf);

// Same constraint, should be able to forward
[[clang::annotate("dtt:requires=arg0 + arg1 < _size")]]
void writeAtWrapper(int off, int len, Buffer& b) {
    // Inside here: off + len < b._size

    // XFAIL: Expression constraints not propagated
    writeAt(off, len, b);  // Should PASS: same precondition
}

// Weaker constraint - should also work
[[clang::annotate("dtt:requires=arg0 < _size")]]
void readAt(int offset, Buffer& buf);

[[clang::annotate("dtt:requires=arg0 + arg1 < _size")]]
void readAtWrapper(int off, int len, Buffer& b) {
    // We know: off + len < b._size
    // Therefore: off < b._size (since len >= 0 assumed)

    // XFAIL: Can't derive simpler bound from complex expression
    readAt(off, b);  // Should PASS: off < off + len < _size
}

int main() { return 0; }
