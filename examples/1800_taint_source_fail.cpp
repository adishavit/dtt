// 1800_taint_source_fail.cpp
// Tests: Taint sources produce errors when reaching sinks
// Multiple variants for different taint source/sink combinations

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Variant 1: getenv -> system (command injection, CWE-78)
void test_command_injection() {
    char* cmd = getenv("USER_CMD");  // cmd is tainted (Medium)
    system(cmd);                     // ERROR: tainted value used as shell command
}

// Variant 2: getenv -> fopen (path traversal, CWE-22)
void test_path_traversal() {
    char* path = getenv("USER_PATH");  // path is tainted (Medium)
    FILE* f = fopen(path, "r");        // ERROR: tainted value used as file path
    if (f) fclose(f);
}

// Variant 3: getenv -> malloc via atoi (tainted allocation, CWE-190)
void test_tainted_alloc() {
    char* sizeStr = getenv("BUF_SIZE"); // sizeStr is tainted (Medium)
    if (!sizeStr) return;               // null check to satisfy existing DTT
    size_t sz = atoi(sizeStr);          // sz is tainted (atoi propagates)
    void* p = malloc(sz);              // ERROR: tainted allocation size
    free(p);
}

int main() { return 0; }
