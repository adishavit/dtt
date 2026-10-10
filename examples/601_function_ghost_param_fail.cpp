// Example 601: Function ghost state - some callers pass unknown
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// When ANY caller passes a pointer with unknown nullability, DTT cannot prove
// the parameter is non-null, so dereferences without null checks produce errors.
//
// This is the CONSERVATIVE behavior: if we can't prove ALL callers pass non-null,
// we must assume the parameter COULD be null.
//
// Expected: FAIL (some callers may pass null → parameter not proven)

struct Data {
    int value;
};

Data* maybeGet();  // External function - unknown return nullability

void process(Data* d) {
    d->value = 42;  // FAIL: some callers may pass null  dtt-error {{potential null pointer dereference}}
}

int main() {
    Data local;
    Data* unknown = maybeGet();

    process(&local);    // Caller 1: non-null (address-of)
    process(unknown);   // Caller 2: UNKNOWN! - breaks the all-nonnull invariant

    return 0;
}
