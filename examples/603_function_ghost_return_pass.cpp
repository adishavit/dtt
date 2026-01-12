// Example 603: Function ghost state - all returns non-null
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// When ALL return statements in a function return non-null values, DTT's
// function ghost state analysis infers @returns_nonnull=1 and injects this
// into the caller, allowing dereferences of the return value without checks.
//
// This is the BACKWARD analysis part of two-pass:
// - Pass 1: Collect all return statements
// - Between passes: Compute @returns_nonnull by checking all returns
// - Pass 2: Inject nullability when assigning from call
//
// Expected: PASS (all returns non-null → return value proven non-null)

struct Data {
    int value;
};

Data* createData() {
    return new Data();  // Only return, always non-null (new throws on failure)
}

int main() {
    Data* p = createData();
    p->value = 42;  // PASS: createData.@returns_nonnull = 1

    delete p;
    return 0;
}
