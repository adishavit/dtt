// 1828_taint_aggregate_array_pass.cpp
// Tests: Arrays with non-tainted elements should NOT trigger taint warnings.

extern "C" {
    int execv(const char *, char *const[]);
    int execvp(const char *, char *const[]);
}

// Variant 1: Constant strings in array (no taint)
void v1_clean_array() {
    char *args[] = {(char *)"ls", (char *)"-la", nullptr};
    execv("/bin/ls", args);  // OK: no tainted elements
}

// Variant 2: Local variables in array (no taint)
void v2_local_array() {
    char prog[] = "/bin/echo";
    char arg1[] = "hello";
    char *args[] = {prog, arg1, nullptr};
    execv(prog, args);  // OK: local strings, not tainted
}

int main() { return 0; }
