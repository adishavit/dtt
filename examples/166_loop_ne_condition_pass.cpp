// Test: Loop with != condition
// Pattern: for (int i = 0; i != 10; i++)
// Should prove: i < 10 inside loop (since starting at 0, incrementing to 10)
// Expected: PASS (iterator-style pattern)

[[clang::annotate("dtt:requires=arg0 < 10")]]
void needsLessThanTen(int x);

void test() {
    for (int i = 0; i != 10; i++) {
        needsLessThanTen(i);  // Should prove i < 10
    }
}
