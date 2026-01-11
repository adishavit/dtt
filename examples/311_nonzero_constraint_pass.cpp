// WHAT IS TESTED:
// Numeric "!= 0" constraints on integers should work correctly.
// They should NOT be treated as pointer-nullness checks.
//
// Expected: PASS (all nonzero values satisfy constraints)

[[clang::annotate("dtt:requires=arg0 != 0")]]
void requires_nonzero(int value) {
    (void)value;
}

[[clang::annotate("dtt:requires=arg0 > 0")]]
void requires_positive(int value) {
    (void)value;
}

[[clang::annotate("dtt:requires=arg0 >= 1")]]
void requires_at_least_one(int value) {
    (void)value;
}

void test_nonzero_positive() {
    requires_nonzero(1);
    requires_nonzero(42);
    requires_nonzero(100);
}

void test_nonzero_negative() {
    requires_nonzero(-1);
    requires_nonzero(-100);
}

void test_positive() {
    requires_positive(1);
    requires_positive(10);
}

void test_at_least_one() {
    requires_at_least_one(1);
    requires_at_least_one(5);
}

int main() {
    test_nonzero_positive();
    test_nonzero_negative();
    test_positive();
    test_at_least_one();
    return 0;
}
