// 1806_taint_unsound_guard_fail.cpp
// Tests: The generic guard-based taint clearing is UNSOUND.
// When a condition involves a tainted variable in a complex expression
// (not matching extractAllGuards), the generic fallback clears ALL
// tainted vars found in the condition — regardless of what the check proves.
//
// A byte-value check like cmd[0] > 100 says NOTHING about whether
// the string is safe for shell execution, but the generic fallback
// clears cmd's taint anyway. This must be fixed.

extern "C" {
    char* getenv(const char*);
    int system(const char*);
}

// Variant 1: Array subscript check — falls to generic fallback
// cmd[0] > 100 is not a simple variable guard (ArraySubscriptExpr,
// not DeclRefExpr), so extractGuard returns nullopt → generic path.
// Generic path finds cmd is tainted in the condition → clears it.
// But checking one byte's VALUE doesn't sanitize CONTENT for shell.
void test_byte_check_command_injection() {
    char* cmd = getenv("CMD");           // cmd tainted (Medium)
    if (!cmd) return;                    // null check
    if (cmd[0] > 100) return;           // complex condition → generic fallback
    system(cmd);                         // ERROR: cmd still tainted for CWE-78
}

int main() { return 0; }
