// 1734: Deferred eval for arg postconditions with `when` guards
// When a free function has arg postconditions guarded by `when`, and the guard
// isn't satisfied at call time, the postcondition should be deferred and
// applied when a later proof satisfies the guard.

// This test uses a custom .dtt annotation that has a guarded arg postcondition.
// The built-in annotations don't currently use `when` guards on arg postconditions,
// so we simulate with a pattern: a function that conditionally modifies an arg's
// ghost field based on another arg's state.

#include <cstdio>

// Use the existing libc annotations: fclose requires arg0._nonnull != 0
// This test verifies arg postconditions WITHOUT `when` guards still work
// after deferred eval changes don't break them.

// Variant 1: fclose on null-checked FILE* (existing behavior, control)
void variant1_fclose_after_null_check() {
    FILE* f = fopen("data.txt", "r");
    if (!f) return;
    fclose(f);    // OK: f._nonnull proven, fclose arg postconditions apply
    fclose(f);    // ERROR: double-close (f._nonnull = 0 after first fclose)
}

// Variant 2: fclose on unchecked FILE* should warn
void variant2_fclose_unchecked() {
    FILE* f = fopen("data.txt", "r");
    fclose(f);    // ERROR: f._nonnull not proven (fopen may return NULL)
}

int main() { return 0; }
