// Test: Loop with mismatched increment direction (soundness)
// Pattern: for (int i = 0; i < 10; --i) - decrement with forward condition
// Should NOT generate proofs (unsound loop)
// Expected: FAIL (DTT error - no proof that i < 10)

[[clang::annotate("dtt:requires=arg0 < 10")]]
void needsLessThanTen(int x);

void test() {
    // BUG: decrementing but forward condition
    // Without direction check, we'd unsoundly prove i < 10
    for (int i = 0; i < 10; --i) {
        needsLessThanTen(i);  // Should fail - no valid proof  dtt-error {{arg0 < 10}}
    }
}
