// 1830_taint_overflow_guard_pass.cpp
// Tests: Sound patterns for guarding tainted arithmetic.
// When the guard is on the original tainted value (not a wrapped result),
// suppression is correct.

extern "C" {
    char *getenv(const char *);
    int atoi(const char *);
    void *malloc(unsigned long);
    void free(void *);
    void *memcpy(void *, const void *, unsigned long);
}

// Variant 1: Guard on original tainted value, then compute
// This is sound: `n` is bounded directly, `n + 16` can't overflow
// when `n <= 4096`.
void v1_guard_before_arithmetic() {
    char *s = getenv("SZ");
    if (!s) return;
    int n = atoi(s);  // tainted
    if (n > 0 && n <= 4096) {
        // n is bounded [1, 4096], so n + 16 <= 4112 (no overflow)
        void *p = malloc(n);  // OK: n directly bounded
        if (p) free(p);
    }
}

// Variant 2: Guard on tainted length for memcpy
void v2_guard_memcpy() {
    char *s = getenv("LEN");
    if (!s) return;
    int len = atoi(s);  // tainted
    char src[256], dst[256];
    if (len > 0 && len <= 256) {
        memcpy(dst, src, len);  // OK: len directly bounded
    }
}

int main() { return 0; }
