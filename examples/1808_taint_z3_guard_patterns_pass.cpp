// 1808_taint_z3_guard_patterns_pass.cpp
// Z3-powered taint guard analysis: all common real-world guard patterns.
// Each variant tests a different condition structure. All should pass —
// Z3 proves the tainted variable is bounded by a non-tainted expression.

extern "C" {
    void *malloc(unsigned long);
    void *memcpy(void*, const void*, unsigned long);
    void free(void*);
    int recv(int, void*, unsigned long, int);
    char *getenv(const char*);
    int atoi(const char*);
}

// Variant 1: Simple comparison (baseline — extractAllGuards handles this too)
void variant_simple_upper_bound() {
    char *env = getenv("SIZE");
    if (!env) return;
    int len = atoi(env);  // tainted

    if (len > 4096) return;  // upper bound

    char *buf = (char*)malloc(len);  // OK: len <= 4096
    if (buf) free(buf);
}

// Variant 2: Negated comparison
// if (!(x < MAX)) return  →  x < MAX after return
void variant_negated() {
    char *env = getenv("COUNT");
    if (!env) return;
    int count = atoi(env);  // tainted

    if (!(count < 1024)) return;  // equivalent to: if (count >= 1024) return

    char *buf = (char*)malloc(count);  // OK: count < 1024
    if (buf) free(buf);
}

// Variant 3: Complex arithmetic
// if (len + 16 > total) return  →  len <= total - 16
void variant_arithmetic() {
    char *env = getenv("LEN");
    if (!env) return;
    int len = atoi(env);  // tainted

    int total = 65536;
    if (len + 16 > total) return;  // len <= total - 16

    char *buf = (char*)malloc(len + 16);  // OK: bounded
    if (buf) free(buf);
}

// Variant 4: Heartbleed pattern (complex expression, member access)
// if (1 + 2 + payload + 16 > rrec.length) return
struct record { unsigned int length; unsigned char *data; };
void variant_heartbleed(record *rec) {
    if (!rec) return;

    recv(0, rec->data, rec->length, 0);  // taints rec->data
    unsigned int payload;
    // Simulate n2s: payload read from tainted data
    payload = (unsigned int)(rec->data[0] << 8 | rec->data[1]);

    if (1 + 2 + payload + 16 > rec->length) return;

    char *buf = (char*)malloc(1 + 2 + payload + 16);  // OK: bounded by rec->length
    if (buf) free(buf);
}

int main() { return 0; }
