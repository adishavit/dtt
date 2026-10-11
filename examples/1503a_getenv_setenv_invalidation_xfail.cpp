// POSIX setenv invalidation; retain the portable getenv/free gap separately.
#include <cstdlib>

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
