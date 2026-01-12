// Example 605: Function ghost state - forward declarations
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// The two-pass architecture handles forward declarations correctly:
// - Pass 1 collects all call sites (even before the function is defined)
// - Pass 2 uses the collected information when analyzing the function body
//
// This tests that call order in the source file doesn't affect analysis.
//
// Expected: PASS (forward declaration + call before definition still works)

struct Data {
    int value;
};

void process(Data* d);  // Forward declaration

int main() {
    Data obj;
    process(&obj);  // Call BEFORE definition - Pass 1 still collects this
    return 0;
}

void process(Data* d) {
    d->value = 42;  // PASS: caller passes non-null (collected in Pass 1)
}
