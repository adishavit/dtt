// Test: Loop with <= condition
// Pattern: for (int i = 0; i <= 9; i++)
// Should prove: i <= 9 implies i < 10
// Expected: PASS (tests if <= bound implies < bound+1)

[[clang::annotate("dtt:requires=arg0 < 10")]]
void needsLessThanTen(int x);

void test() {
    // Using <= 9 which should imply < 10
    for (int i = 0; i <= 9; i++) {
        needsLessThanTen(i);  // Requires proof that i < 10
    }
}
