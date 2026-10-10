// Example 611: Ghost state conditional nullability - FAIL case
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// When a function has "if (!param) return nullptr;" and some callers pass
// unknown nullability, the nullptr return path IS reachable, so the
// function's return value cannot be proven non-null.
//
// Expected: FAIL (nullptr path is reachable since caller passes unknown)

struct Data {
    int value;
};

Data* getExternalData();  // Unknown nullability

Data* getOrNull(Data* input) {
    if (!input) return nullptr;  // Guarded by null check on param 0
    return input;
}

int main() {
    Data* p = getOrNull(getExternalData());  // Caller passes UNKNOWN
    p->value = 42;  // FAIL: nullptr path is reachable  dtt-error {{potential null pointer dereference}}
    return 0;
}
