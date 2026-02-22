// Output parameter annotation — should COMPILE but currently FAILS
//
// WHAT IS TESTED:
// A dedicated annotation for output parameters that establishes postconditions
// on the parameter itself, not just the return value.
//
// WHY IT'S DIFFICULT:
// 1. Need new annotation syntax: [[dtt:out=_size > 0]] or similar
// 2. Must track which parameters are "output" vs "input/output"
// 3. Caller must treat output param as uninitialized before call
// 4. After call, output param has the declared properties
//
// RATIONALE:
// Many C APIs use output parameters instead of return values:
// - Error codes returned, actual result in out-param
// - Multiple outputs from single function
// - Reusing buffers for efficiency
//
// CURRENT BEHAVIOR:
// No way to declare postconditions on parameters. Callers must manually
// add guards after every call that initializes via output parameter.
//
// PROPOSED SYNTAX:
// [[clang::annotate("dtt:out=_size > 0")]]  // output param gets this property
// [[clang::annotate("dtt:inout=_size")]]    // input/output, preserves property

struct Buffer {
    int* data;
    int _size;
};

// Initializes buf with at least 'count' elements
// Proposed: [[clang::annotate("dtt:out:arg0=_size >= arg1")]]
void initBuffer(Buffer& buf, int count);

[[clang::annotate("dtt:requires=_size > 0")]]
void processBuffer(Buffer& buf);

int main() {
    Buffer b;
    // b._size is uninitialized/unknown

    initBuffer(b, 10);  // Should establish: b._size >= 10

    // XFAIL: b._size is unknown (no output param annotation support)
    processBuffer(b);  // Should PASS: initBuffer guarantees _size >= 10

    return 0;
}
