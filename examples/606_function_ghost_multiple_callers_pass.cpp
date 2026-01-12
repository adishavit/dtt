// Example 606: Function ghost state - multiple callers all pass non-null
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// Tests that the ghost state analysis correctly handles multiple callers
// from different call sites, all passing non-null values.
//
// This ensures the analysis correctly iterates through all CallSiteInfo
// entries and only marks @paramN_nonnull=1 when ALL callers are non-null.
//
// Expected: PASS (all 4 callers pass non-null → parameter proven non-null)

struct Data {
    int value;
};

void process(Data* d) {
    d->value = 42;  // PASS: all callers proven non-null
}

void caller1() {
    Data d;
    process(&d);  // Caller 1: address-of
}

void caller2() {
    Data d;
    process(&d);  // Caller 2: address-of
}

void caller3(Data& ref) {
    process(&ref);  // Caller 3: address-of reference
}

int main() {
    Data local;
    caller1();
    caller2();
    caller3(local);
    process(&local);  // Caller 4: address-of
    return 0;
}
