// 1503: getenv() returns internal pointer that must NOT be freed - DTT gap
// getenv returns a pointer to an internal environment string.
// Calling free() on it is undefined behavior. DTT can track "must free"
// (via @allocated) but cannot express "must NOT free" constraints.

#include <cstdlib>

// Variant 1: Free the getenv result - UB
void variant1_free_getenv_result() {
    char* path = getenv("PATH");
    if (path) {
        free(path);  // BUG: UB - getenv returns internal pointer, not malloc'd
    }
}

// Variant 2: getenv result invalidated by setenv
void variant2_getenv_invalidated_by_setenv() {
    char* val = getenv("MY_VAR");
    if (!val) return;

    // setenv may reallocate the environment, invalidating previous getenv pointers
    setenv("OTHER_VAR", "something", 1);

    // val may now be a dangling pointer (implementation-defined)
    char c = val[0];  // BUG: potentially dangling after setenv
    (void)c;
}

int main() { return 0; }
