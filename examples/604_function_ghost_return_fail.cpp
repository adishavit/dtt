// Example 604: Function ghost state - some returns can be null
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// When ANY return path can return null (nullptr), DTT cannot prove the return
// value is non-null, so dereferences without null checks produce errors.
//
// This is the CONSERVATIVE behavior: if we can't prove ALL returns are non-null,
// we must assume the return value COULD be null.
//
// Expected: FAIL (some paths return null → return value not proven)

struct Data {
    int value;
};

Data* maybeCreate(bool flag) {
    if (flag) {
        return new Data();  // Non-null path
    }
    return nullptr;  // One path returns null - breaks @returns_nonnull
}

int main() {
    Data* p = maybeCreate(true);
    p->value = 42;  // FAIL: maybeCreate can return null  dtt-error {{potential null pointer dereference}}

    return 0;
}
