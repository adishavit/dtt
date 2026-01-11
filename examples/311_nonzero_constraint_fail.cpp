// WHAT IS TESTED:
// Numeric constraints should catch violations.
//
// Expected: ERROR (constraints violated)

[[clang::annotate("dtt:requires=arg0 != 0")]]
void requires_nonzero(int value) {
    (void)value;
}

void test_zero_violation() {
    requires_nonzero(0);  // ERROR: 0 violates arg0 != 0
}

int main() { test_zero_violation(); return 0; }
