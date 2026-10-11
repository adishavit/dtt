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

int main() { return 0; }
