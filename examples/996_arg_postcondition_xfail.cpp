// Example 996: Unified arg postcondition syntax - xfail (gap: arg0 not supported for non-void)
// This test documents the current limitation that ensures=arg0.@field
// is not parsed for non-void functions.
//
// When this feature is implemented, rename to 996_arg_postcondition_fail.cpp

struct Handle {};

// Acquisition: sets result.@open = 1
[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
Handle* acquire();

// Release: SHOULD set arg0.@open = 0, but currently doesn't parse
[[clang::annotate("dtt:requires=arg0.@open == 1")]]
[[clang::annotate("dtt:ensures=arg0.@open == 0")]]  // Currently ignored!
int release(Handle* h);

void test_gap() {
    Handle* h = acquire();
    release(h);  // arg0.@open = 0 not applied due to parsing gap
}  // ERROR: ~requires violated because @open still == 1

int main() {
    return 0;
}
