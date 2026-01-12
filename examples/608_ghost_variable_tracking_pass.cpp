// Example 608: Ghost state variable tracking (Gap 1)
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// When a pointer variable is initialized with a non-null value and then
// passed to a function, the ghost state should recognize the variable as
// non-null even though it's passed as a DeclRefExpr, not the original expr.
//
// This tests the variable tracking improvement in Pass 1.
//
// Expected: PASS (variable p is tracked as non-null from initialization)

struct Data {
    int value;
};

void process(Data* d) {
    d->value = 42;  // PASS: p was initialized with new, tracked in caller
}

int main() {
    Data* p = new Data();  // p._nonnull = 1 tracked in varNullability
    process(p);            // Ghost state looks up p in caller's varNullability
    delete p;
    return 0;
}
