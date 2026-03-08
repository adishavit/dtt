// 1829_taint_overflow_guard_fail.cpp
// Tests: Taint guards involving arithmetic that can overflow must NOT
// suppress warnings. Z3 must reason with wrapping semantics.
//
// The guard `if (w * h <= MAX)` is unsound because w * h can overflow
// to a small value, passing the check while the mathematical product
// is actually huge.

extern "C" {
    char *getenv(const char *);
    int atoi(const char *);
    void *malloc(unsigned long);
    void free(void *);
    void *memcpy(void *, const void *, unsigned long);
}

// Variant 1: Multiplication overflow bypasses guard
// Attacker sets w=65536, h=65536 → w*h = 0 (32-bit overflow)
// Guard passes (0 <= 1000000), but actual memory need is 4GB
void v1_mul_overflow_guard() {
    char *ws = getenv("W");
    char *hs = getenv("H");
    if (!ws || !hs) return;
    unsigned int w = (unsigned int)atoi(ws);  // tainted
    unsigned int h = (unsigned int)atoi(hs);  // tainted
    unsigned int total = w * h;               // may overflow
    if (total <= 1000000) {
        void *p = malloc(total);              // ERROR: tainted allocation size
        if (p) free(p);                       //   (guard unsound due to overflow)
    }
}

// Variant 2: Addition overflow bypasses guard
// Attacker sets sz=0xFFFFFFFF → sz+16 = 15 (32-bit overflow)
// Guard passes (15 <= 4096), but caller expects sz bytes
void v2_add_overflow_guard() {
    char *s = getenv("SZ");
    if (!s) return;
    unsigned int sz = (unsigned int)atoi(s);  // tainted
    unsigned int total = sz + 16;             // may overflow
    if (total <= 4096) {
        void *p = malloc(total);              // ERROR: tainted allocation
        if (p) free(p);
    }
}

int main() { return 0; }
