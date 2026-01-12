// Example 607: Function ghost state with dtt:ensures annotation
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// Explicit dtt:ensures annotations take precedence over ghost state inference.
// Even if ghost state analysis couldn't prove return nullability, an explicit
// annotation should be trusted.
//
// This tests that existing annotation infrastructure works alongside ghost state.
//
// Expected: PASS (dtt:ensures annotation trusted)

struct Data {
    int value;
};

// Explicit annotation says result is non-null, even though we can't infer it
[[clang::annotate("dtt:ensures=result._nonnull=1")]]
Data* createData(bool flag) {
    if (flag) {
        return new Data();
    }
    // In practice, this path might not be reachable if flag is always true
    // The annotation tells DTT to trust us
    return new Data();
}

int main() {
    Data* p = createData(true);
    p->value = 42;  // PASS: dtt:ensures annotation trusted

    delete p;
    return 0;
}
