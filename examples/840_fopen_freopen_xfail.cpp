// 840_fopen_freopen_xfail.cpp
// XFAIL - freopen reassigns FILE* variable, confusing scope_exit tracking
// When fp = freopen(...) reassigns the variable, DTT still has a scope_exit
// from the original fopen. The variable reassignment is not recognized as
// transferring or replacing the resource, causing a spurious scope_exit error.

#include <cstdio>

// Variant 7.1: freopen and check (extracted from 840_fopen_pass.cpp)
void test_freopen_guard() {
    FILE* fp = fopen("test.txt", "r");
    if (fp) {
        fp = freopen("test2.txt", "r", fp);
        if (fp) {
            char buf[100];
            fgets(buf, sizeof(buf), fp);  // OK: double guarded
            fclose(fp);
        }
    }
}

int main() { return 0; }
