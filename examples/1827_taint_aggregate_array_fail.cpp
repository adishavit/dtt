// 1827_taint_aggregate_array_fail.cpp
// Tests: Tainted values inside arrays must be detected at sinks.
// The array itself should be considered tainted when it contains tainted elements.

extern "C" {
    char *getenv(const char *);
    int execv(const char *, char *const[]);
    int execvp(const char *, char *const[]);
    int execve(const char *, char *const[], char *const[]);
    int system(const char *);
}

// Variant 1: Array initializer with tainted element -> execv argv
void v1_array_init_execv() {
    char *cmd = getenv("CMD");          // tainted
    char *args[] = {cmd, nullptr};      // args contains tainted element
    execv("/bin/sh", args);             // ERROR: tainted program argv (CWE-78)
}

// Variant 2: Array initializer with tainted element -> execvp
void v2_array_init_execvp() {
    char *path = getenv("PATH");        // tainted
    char *args[] = {path, nullptr};
    execvp(path, args);                 // ERROR: tainted path AND tainted argv
}

// Variant 3: Multiple tainted elements in array
void v3_multi_tainted() {
    char *a = getenv("A");              // tainted
    char *b = getenv("B");              // tainted
    char *args[] = {a, b, nullptr};
    execv("/bin/sh", args);             // ERROR: tainted argv
}

// Variant 4: Tainted element through assignment chain
void v4_chain_in_array() {
    char *env = getenv("X");
    char *copy = env;                   // still tainted
    char *args[] = {copy, nullptr};
    execv("/bin/sh", args);             // ERROR: tainted argv
}

int main() { return 0; }
