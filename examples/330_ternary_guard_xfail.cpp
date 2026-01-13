// Tests: Ternary with guarded variables (remaining gap)
// Issue #23 partial: DTT doesn't propagate guarded non-null through ternary
// Gap: inferNonnullFromExpr doesn't have access to varNullability for DeclRefExprs

struct Data {
    int value;
};

// Guarded variables in ternary - both are non-null after guard
void variant1_guarded_ternary(Data* ptr, Data* fallback, bool pick) {
    if (!ptr || !fallback) return;
    // ptr and fallback are now guarded non-null
    Data* chosen = pick ? ptr : fallback;
    chosen->value = 1;  // gap: both operands are guarded non-null
}

int main() {
    Data d1, d2;
    variant1_guarded_ternary(&d1, &d2, true);
    return 0;
}
