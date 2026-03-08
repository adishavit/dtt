// 1809_taint_z3_tainted_bound_fail.cpp
// Z3 taint guard: bound comes from ANOTHER tainted variable.
// if (tainted_len > tainted_max) return — the bound is meaningless
// because tainted_max is itself untrusted. Should still warn.

extern "C" {
    void *malloc(unsigned long);
    void free(void*);
    char *getenv(const char*);
    int atoi(const char*);
}

void tainted_bound_is_meaningless() {
    char *env1 = getenv("LEN");
    char *env2 = getenv("MAX");
    if (!env1 || !env2) return;

    int len = atoi(env1);  // tainted
    int max = atoi(env2);  // also tainted!

    if (len > max) return;  // bound from tainted source — meaningless

    char *buf = (char*)malloc(len);  // ERROR: len still tainted, bound is tainted too
    if (buf) free(buf);
}

int main() { return 0; }
