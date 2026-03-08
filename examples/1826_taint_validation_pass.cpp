// 1826_taint_validation_pass.cpp
// Comprehensive taint tracking validation: proves DTT correctly SUPPRESSES
// taint warnings when proper guards, sanitizers, or clean values are used.
// Each variant matches a corresponding variant in 1825_*_fail.cpp.

extern "C" {
    char *getenv(const char *);
    int atoi(const char *);
    void *malloc(unsigned long);
    void *calloc(unsigned long, unsigned long);
    void free(void *);
    void *memcpy(void *, const void *, unsigned long);
    void *memmove(void *, const void *, unsigned long);
    char *strncpy(char *, const char *, unsigned long);
    int recv(int, void *, unsigned long, int);
    int system(const char *);
    int printf(const char *, ...);
    int sprintf(char *, const char *, ...);
    void *dlopen(const char *, int);
    void *alloca(unsigned long);
    char *strdup(const char *);
}

// === Clean values: no taint source, no warnings ===

// Variant 1: Constant string -> system (no taint)
void v1_constant_system() {
    system("ls -la");  // OK: string literal, not tainted
}

// Variant 2: Local computation -> printf format (no taint)
void v2_local_printf() {
    const char *fmt = "Hello %s\n";
    printf(fmt, "world");  // OK: local string, not tainted
}

// Variant 3: Local value -> malloc (no taint)
void v3_local_malloc() {
    int n = 42;
    void *p = malloc(n);  // OK: local constant, not tainted
    if (p) free(p);
}

// Variant 4: Local value -> memcpy length (no taint)
void v4_local_memcpy() {
    char src[64] = "hello";
    char dst[64];
    memcpy(dst, src, 5);  // OK: literal length, not tainted
}

// === Sanitizer attribute: taint cleared ===

[[clang::annotate("dtt:sanitizes=retval")]]
char *sanitize_input(char *input);

// Variant 5: getenv -> sanitizer -> system (taint cleared)
void v5_sanitized_system() {
    char *cmd = getenv("CMD");
    char *safe = sanitize_input(cmd);
    system(safe);  // OK: sanitized
}

// === Z3 guard patterns: bounded tainted values ===

// Variant 6: getenv -> atoi -> bounded by constant -> malloc
void v6_bounded_malloc() {
    char *s = getenv("SZ");
    if (!s) return;
    int n = atoi(s);
    if (n > 0 && n <= 4096) {
        void *p = malloc(n);  // OK: bounded [1, 4096]
        if (p) free(p);
    }
}

// Variant 7: getenv -> atoi -> bounded -> memcpy length
void v7_bounded_memcpy() {
    char *s = getenv("LEN");
    if (!s) return;
    int len = atoi(s);
    char src[256], dst[256];
    if (len > 0 && len <= 256) {
        memcpy(dst, src, len);  // OK: bounded [1, 256]
    }
}

// Variant 8: getenv -> atoi -> bounded -> calloc
void v8_bounded_calloc() {
    char *s = getenv("N");
    if (!s) return;
    unsigned long n = (unsigned long)atoi(s);
    if (n <= 1024) {
        void *p = calloc(n, sizeof(int));  // OK: bounded [0, 1024]
        if (p) free(p);
    }
}

// Variant 9: recv -> bounded length -> memcpy
void v9_bounded_recv_memcpy() {
    char buf[1024];
    int n = recv(0, buf, sizeof(buf), 0);
    char dst[256];
    if (n > 0 && n <= 256) {
        memcpy(dst, buf, n);  // OK: n bounded [1, 256]
    }
}

// === Heartbleed fix pattern: tainted bounded by non-tainted ===

struct Record {
    unsigned int length;
    unsigned char *data;
};

// Variant 10: Heartbleed-style fix (payload bounded by record length)
void v10_heartbleed_fix(Record *rec) {
    if (!rec) return;
    recv(0, rec->data, rec->length, 0);  // data becomes tainted

    unsigned int payload;
    unsigned char *p = rec->data;
    payload = (p[0] << 8) | p[1];  // payload from tainted data

    // THE FIX: bound payload by non-tainted record length
    if (payload + 3 > rec->length)
        return;

    unsigned char *buf = (unsigned char *)malloc(payload + 3);  // OK: bounded
    if (!buf) return;
    memcpy(buf, p + 2, payload);  // OK: payload bounded
    free(buf);
}

int main() { return 0; }
