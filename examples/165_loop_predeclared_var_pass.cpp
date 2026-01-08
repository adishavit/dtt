// Test: Loop with pre-declared variable
// Pattern: int i; for (i = 0; i < 10; i++)
// Should prove: i < 10 inside loop
// Expected: PASS (common C-style pattern)

[[clang::annotate("dtt:requires=arg0 < 10")]]
void needsLessThanTen(int x);

void test() {
    int i;
    for (i = 0; i < 10; i++) {
        needsLessThanTen(i);  // Should prove i < 10
    }
}
