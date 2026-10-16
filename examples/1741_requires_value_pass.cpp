// 1741: Argument-value preconditions, attribute and .dtt forms alike
//
// Each call satisfies requires arg0 >= 0, from its value or from a guard.
// Runs in enforce mode, so a call DTT cannot verify fails the test.

void take_count(int n);
[[clang::annotate("dtt:requires=n >= 0")]] void take_size(int n);

// Variant 1: constants.
void variant1_constant() {
    take_count(0);
    take_size(7);
}

// Variant 2: early return on the negated precondition.
void variant2_early_return(int n) {
    if (n < 0) return;
    take_count(n);
    take_size(n);
}

// Variant 3: call inside the guarded branch.
void variant3_then_branch(int n) {
    if (n >= 0) {
        take_count(n);
        take_size(n);
    }
}

// Variant 4: a stronger guard (n >= 1 implies n >= 0).
void variant4_stronger_guard(int n) {
    if (n < 1) return;
    take_count(n);
    take_size(n);
}

int main() { return 0; }
