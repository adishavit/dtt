// WHAT IS TESTED:
// Numeric "!= 0" constraints should not be treated as pointer-nullness.
// The current parser treats "arg0 != 0" as a nullptr constraint and emits
// nullability diagnostics even for integers.
//
// Expected behavior: PASS (arg0 != 0 holds)
// Current behavior: FAIL (nullability error on integer)

[[clang::annotate("dtt:requires=arg0 != 0")]]
void requires_nonzero(int value) {
    (void)value;
}

void test() {
    requires_nonzero(1);
}

int main() { test(); return 0; }
