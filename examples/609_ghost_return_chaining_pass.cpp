// Example 609: Ghost state return value chaining (Gap 2)
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// When a function returns non-null and its return value is passed directly
// to another function, the ghost state should chain the nullability:
// create() returns non-null → use(create()) knows param is non-null
//
// This tests the return value chaining improvement.
//
// Expected: PASS (create's @returns_nonnull chains to use's @param0_nonnull)

struct Data {
    int value;
};

Data* create() {
    return new Data();  // @returns_nonnull = 1
}

void use(Data* d) {
    d->value = 42;  // PASS: create() returns non-null, chained via ghost state
}

int main() {
    use(create());  // Arg is CallExpr, ghost state chains @returns_nonnull
    return 0;
}
