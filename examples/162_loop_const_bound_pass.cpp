// Test: Loop with constant upper bound
// Pattern: for (int i = 0; i < 10; i++)
// Should prove: i < 10 via loop bound
// Expected: PASS (tests if constant bounds are tracked)

[[clang::annotate("dtt:requires=arg0 < 10")]]
void needsLessThanTen(int x);

void test() {
    for (int i = 0; i < 10; i++) {
        needsLessThanTen(i);  // Requires proof that i < 10
    }
}
