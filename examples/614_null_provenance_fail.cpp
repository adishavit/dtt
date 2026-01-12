// Example 614: Null provenance tracking
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// When a null dereference error is reported for a pointer parameter,
// DTT now emits an additional note showing WHERE the nullability uncertainty
// originated. This helps users understand the root cause:
// - For parameters: points to the parameter declaration
//
// Expected: FAIL (errors with provenance notes pointing to parameter decls)

struct MyStruct { int x; };

void processPointer(int* data) {
    // ERROR: data could be NULL
    // NOTE: 'data' may be NULL because: parameter 'data' may be NULL (callers could pass NULL)
    *data = 42;
}

void processArrow(MyStruct* ptr) {
    // ERROR: ptr could be NULL
    // NOTE: 'ptr' may be NULL because: parameter 'ptr' may be NULL (callers could pass NULL)
    ptr->x = 10;
}

int main() {
    processPointer(nullptr);  // Valid call - caller can pass NULL
    return 0;
}
