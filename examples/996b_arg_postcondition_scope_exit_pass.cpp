// Example 996b: Unified arg postcondition syntax with scope-exit - should pass
// Tests that ensures=arg0.@field works for non-void functions,
// combined with ~requires scope-exit constraints.

struct Handle {};

// Acquisition: sets result.@open = 1
[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
Handle* acquire();

// Release: sets arg0.@open = 0 (works for any return type now)
[[clang::annotate("dtt:requires=arg0.@open == 1")]]
[[clang::annotate("dtt:ensures=arg0.@open == 0")]]
int release(Handle* h);

void test_unified_syntax() {
    Handle* h = acquire();
    release(h);  // Sets h.@open = 0
}  // OK: ~requires satisfied because @open == 0

int main() {
    return 0;
}
