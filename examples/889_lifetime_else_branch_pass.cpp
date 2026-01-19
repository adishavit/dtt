// 889_lifetime_else_branch_xfail.cpp
// Tests: PointerLifetimes state should NOT leak from then-branch into else-branch
// Bug: else-branch starts from post-then state instead of pre-if state
// This causes false positives (valid pointer seen as freed)

#include <cstdlib>

// Variant 1: Then-branch frees and returns, fall-through continues
// The fall-through path is the implicit else - p should still be valid
void test_then_frees_and_returns(int cond) {
    int* p = (int*)malloc(sizeof(int));
    if (!p) return;

    if (cond) {
        free(p);
        return;  // Then terminates
    }
    // Implicit else: cond was false, so we never freed p
    // BUG: DTT incorrectly sees p as freed due to state leak
    *p = 42;  // Should be OK - p is still valid here
    free(p);  // Should be OK - first free on this path
}

// Variant 2: Explicit else-branch after then frees and returns
void test_explicit_else_after_free(int cond) {
    int* p = (int*)malloc(sizeof(int));
    if (!p) return;

    if (cond) {
        free(p);
        return;
    } else {
        // Explicit else: p was never freed on this path
        *p = 42;  // Should be OK
        free(p);  // Should be OK
    }
}

// Variant 3: Then-branch frees (no return), else continues
// This is trickier - after if-else, p's state depends on which branch ran
// But WITHIN the else-block, p should definitely be valid
void test_then_frees_no_return(int cond) {
    int* p = (int*)malloc(sizeof(int));
    if (!p) return;

    if (cond) {
        free(p);
        // No return - continues after if
    } else {
        // Within else: p is definitely valid (cond was false)
        *p = 42;  // Should be OK
        free(p);  // Should be OK
    }
    // After if-else: p might be freed (either branch could have run)
    // Don't access p here
}

// Variant 4: Nested conditions - deeper state leak potential
void test_nested_conditions(int a, int b) {
    int* p = (int*)malloc(sizeof(int));
    if (!p) return;

    if (a) {
        if (b) {
            free(p);
            return;
        }
        // Here: a is true, b is false, p is valid
        *p = 1;  // Should be OK
    } else {
        // Here: a is false, p is valid
        *p = 2;  // Should be OK
    }
    // After: depends on path, but if we reach here p might be valid
    free(p);  // Might be double-free if a && b path didn't return...
              // Actually this is fine because a && b returns
}

int main() { return 0; }
