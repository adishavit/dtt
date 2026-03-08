// 1825_taint_validation_fail.cpp
// Comprehensive taint tracking validation: proves DTT catches tainted data
// reaching ALL sink categories through diverse source/propagation paths.
// Each variant tests a unique (source, propagation, sink) triple.

extern "C" {
    char *getenv(const char *);
    int atoi(const char *);
    void *malloc(unsigned long);
    void *calloc(unsigned long, unsigned long);
    void free(void *);
    void *memcpy(void *, const void *, unsigned long);
    void *memmove(void *, const void *, unsigned long);
    char *strncpy(char *, const char *, unsigned long);
    char *strncat(char *, const char *, unsigned long);
    int recv(int, void *, unsigned long, int);
    int system(const char *);
    int execl(const char *, const char *, ...);
    int execvp(const char *, char *const[]);
    int printf(const char *, ...);
    int fprintf(void *, const char *, ...);
    int sprintf(char *, const char *, ...);
    void *dlopen(const char *, int);
    void *alloca(unsigned long);
    char *strdup(const char *);
    char *fgets(char *, int, void *);
    int scanf(const char *, ...);
    int sscanf(const char *, const char *, ...);
}

// === CWE-78: Command Injection ===

// Variant 1: getenv (Medium) -> direct -> system
void v1_getenv_system() {
    char *cmd = getenv("CMD");
    system(cmd);  // ERROR: tainted shell command
}

// Variant 2: getenv -> assignment chain -> execvp
void v2_getenv_chain_exec() {
    char *a = getenv("X");
    char *b = a;
    char *c = b;
    char *args[] = {c, nullptr};
    execvp(c, args);  // ERROR: tainted program path
}

// Variant 3: getenv -> atoi -> cast -> back to pointer context -> exec
void v3_getenv_cast_exec() {
    char *env = getenv("PATH");
    execl(env, env, (char *)0);  // ERROR: tainted program path
}

// === CWE-134: Format String Injection ===

// Variant 4: getenv -> printf format (format string injection)
void v4_getenv_printf_format() {
    char *fmt = getenv("FMT");
    printf(fmt);  // ERROR: tainted format string
}

// Variant 5: getenv -> sprintf format
void v5_getenv_sprintf_format() {
    char buf[256];
    char *fmt = getenv("FMT");
    sprintf(buf, fmt);  // ERROR: tainted format string
}

// === CWE-190: Integer Overflow (tainted allocation) ===

// Variant 6: getenv -> atoi -> malloc
void v6_getenv_atoi_malloc() {
    char *s = getenv("SZ");
    if (!s) return;
    int n = atoi(s);
    void *p = malloc(n);  // ERROR: tainted allocation size
    if (p) free(p);
}

// Variant 7: getenv -> atoi -> C-style cast -> calloc
void v7_getenv_cast_calloc() {
    char *s = getenv("SZ");
    if (!s) return;
    unsigned long n = (unsigned long)atoi(s);
    void *p = calloc(n, 1);  // ERROR: tainted allocation size
    if (p) free(p);
}

// Variant 8: getenv -> atoi -> alloca (stack allocation)
void v8_getenv_alloca() {
    char *s = getenv("SZ");
    if (!s) return;
    int n = atoi(s);
    void *p = alloca(n);  // ERROR: tainted stack allocation size
    (void)p;
}

// === CWE-120: Buffer Copy with Tainted Length ===

// Variant 9: getenv -> atoi -> memcpy length
void v9_getenv_memcpy_len() {
    char *s = getenv("LEN");
    if (!s) return;
    int len = atoi(s);
    char src[256], dst[256];
    memcpy(dst, src, len);  // ERROR: tainted copy length
}

// Variant 10: getenv -> atoi -> strncpy length
void v10_getenv_strncpy_len() {
    char *s = getenv("LEN");
    if (!s) return;
    int len = atoi(s);
    char src[256], dst[256];
    strncpy(dst, src, len);  // ERROR: tainted copy length
}

// Variant 11: getenv -> atoi -> static_cast -> memmove length
void v11_getenv_static_cast_memmove() {
    char *s = getenv("LEN");
    if (!s) return;
    unsigned long len = static_cast<unsigned long>(atoi(s));
    char buf[256];
    memmove(buf, buf + 10, len);  // ERROR: tainted copy length
}

// === CWE-426: Untrusted Library Path ===

// Variant 12: getenv -> dlopen
void v12_getenv_dlopen() {
    char *lib = getenv("LIB_PATH");
    dlopen(lib, 1);  // ERROR: tainted library path
}

// === CWE-22: Path Traversal ===

// Variant 13: getenv -> strdup -> exec (propagation through strdup)
void v13_getenv_strdup_exec() {
    char *env = getenv("X");
    if (!env) return;
    char *dup = strdup(env);
    system(dup);  // ERROR: tainted shell command
    free(dup);
}

// === Network source (High taint) ===

// Variant 14: recv -> memcpy (Heartbleed pattern)
void v14_recv_memcpy() {
    char buf[1024];
    int n = recv(0, buf, sizeof(buf), 0);
    if (n <= 0) return;
    // buf now tainted (High), n also tainted
    char dst[256];
    memcpy(dst, buf, n);  // ERROR: tainted copy length
}

// === argv source ===

// Variant 15: argv -> system
int main(int argc, char *argv[]) {
    if (argc > 1 && argv[1]) {
        system(argv[1]);  // ERROR: tainted shell command
    }
    return 0;
}
