// Example 600: Function ghost state - all callers pass non-null
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// When ALL callers of a function pass non-null arguments, DTT's function ghost
// state analysis infers @paramN_nonnull=1 and injects this into the function
// body, allowing parameter dereferences without explicit null checks.
//
// This is the primary use case for the two-pass bidirectional analysis:
// - Pass 1: Collect all call sites
// - Between passes: Compute @paramN_nonnull by checking all callers
// - Pass 2: Inject nullability into parameters on function entry
//
// Expected: PASS (all callers pass non-null → parameter proven non-null)

struct Data {
    int value;
};

void process(Data* d) {
    d->value = 42;  // PASS: ghost state proves d is non-null
}

int main() {
    Data local;
    Data another;

    process(&local);   // Caller 1: &local is non-null (address-of)
    process(&another); // Caller 2: &another is non-null (address-of)

    return 0;
}
