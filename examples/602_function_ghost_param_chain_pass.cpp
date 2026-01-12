// Example 602: Function ghost state - derived pointers inherit nullability
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// When a parameter is proven non-null via ghost state, any derived pointers
// (assignments like `Data* local = d;`) should inherit the non-null property.
//
// This tests that ghost state integrates correctly with DTT's existing
// pointer copy/assignment tracking.
//
// Expected: PASS (d proven non-null → local and other inherit non-null)

struct Data {
    int value;
};

void process(Data* d) {
    Data* local = d;      // local derives from d
    Data* other = local;  // other derives from local
    other->value = 42;    // PASS: d proven non-null → chain is non-null
}

int main() {
    Data obj;
    process(&obj);  // Only caller, passes non-null (address-of)
    return 0;
}
