// Example 610: Ghost state conditional nullability (Gap 3)
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// When a function has "if (!param) return nullptr;" but all callers pass
// non-null for that param, the nullptr return is unreachable, so the
// function effectively always returns non-null.
//
// This tests the conditional nullability improvement.
//
// Expected: PASS (nullptr return is unreachable, so p is non-null)

struct Data {
    int value;
};

Data* getOrNull(Data* input) {
    if (!input) return nullptr;  // Guarded by null check on param 0
    return input;                // Non-null path
}

int main() {
    Data local;
    Data* p = getOrNull(&local);  // Caller passes non-null
    p->value = 42;                // PASS: nullptr path unreachable
    return 0;
}
