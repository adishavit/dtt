// 1502: realpath() conditional allocation - DTT gap
// When called with NULL as second argument, realpath() returns a malloc'd
// string that must be freed. DTT cannot express conditional resource
// acquisition (allocates only when arg is NULL).

#include <cstdlib>
#include <climits>

// Variant 1: realpath with NULL resolved_path - leaks
void variant1_realpath_null_leak() {
    char* resolved = realpath("/tmp", nullptr);  // malloc's result internally
    // BUG: resolved is allocated but never freed - resource leak
    (void)resolved;
}

// Variant 2: realpath with provided buffer - no leak (different semantics)
void variant2_realpath_buf_no_leak() {
    char buf[PATH_MAX];
    char* resolved = realpath("/tmp", buf);  // Uses provided buffer, no malloc
    // OK: no allocation to free
    (void)resolved;
}

int main() { return 0; }
