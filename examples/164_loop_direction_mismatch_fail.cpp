// Test: Loop with mismatched increment direction (soundness)
// Pattern: for (int i = 0; i < 10; --i) - decrement with forward condition
// The loop shape must NOT give i the range [0, 10): i decreases from 0.
// (i < 10 does hold in the body: the condition guarantees it.)
// Expected: FAIL (DTT error - no proof that i >= 0)

[[clang::annotate("dtt:requires=arg0 < 10")]]
void needsLessThanTen(int x);

[[clang::annotate("dtt:requires=arg0 >= 0")]]
void needsNonNegative(int x);

void test() {
    // BUG: decrementing but forward condition
    // Without direction check, we'd unsoundly prove i >= 0
    for (int i = 0; i < 10; --i) {
        needsLessThanTen(i);  // OK: the condition holds in the body
        needsNonNegative(i);  // Should fail - i goes negative  dtt-error {{arg0 >= 0}}
    }
}

// Same with i -= 2
void test_step() {
    for (int i = 0; i < 10; i -= 2) {
        needsNonNegative(i);  // dtt-error {{arg0 >= 0}}
    }
}

int main() {
    test();
    test_step();
    return 0;
}
