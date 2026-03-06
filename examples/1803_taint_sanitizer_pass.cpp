// 1803_taint_sanitizer_pass.cpp
// Tests: User-defined sanitizer functions clear taint
// After sanitization, values should not trigger taint warnings

#include <cstdlib>
#include <cstring>

// User declares a sanitizer via dtt annotation attribute
[[clang::annotate("dtt:sanitizes=retval")]]
char* validate_command(char* cmd);

// Variant 1: Sanitized return value is clean
void test_sanitized_retval() {
    char* cmd = getenv("CMD");           // cmd is tainted
    char* safe = validate_command(cmd);  // safe is NOT tainted (sanitized)
    system(safe);                        // OK: sanitized
}

int main() { return 0; }
