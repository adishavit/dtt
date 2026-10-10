// WHAT IS TESTED:
// Numeric "!= 0" constraints on integer parameters should be enforced.
// The current parser silently ignores "arg0 != 0" for non-pointer types.
//
// Expected behavior: ERROR (arg0 != 0 violated by passing 0)
// Current behavior: SILENT (constraint ignored entirely)
//
// Note: This is labeled xfail but currently passes silently (false negative).
// When fixed, it should produce an error and remain xfail, OR be split into
// a _pass test (valid call) and _fail test (invalid call with 0).

[[clang::annotate("dtt:requires=arg0 != 0")]]
void requires_nonzero(int value) {
    (void)value;
}

void test() {
    requires_nonzero(0);  // Should ERROR: violates arg0 != 0  dtt-error {{arg0 != 0}}
}

int main() { test(); return 0; }
