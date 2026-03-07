// 1810_taint_cast_propagation_fail.cpp
// Taint must propagate through C-style casts and static_cast.
// Previously, (unsigned int)atoi(env) lost taint because VisitVarDecl
// used IgnoreParenImpCasts() which doesn't skip explicit casts.

extern "C" {
    void *malloc(unsigned long);
    void free(void*);
    char *getenv(const char*);
    int atoi(const char*);
}

// Variant 1: C-style cast
void variant_c_cast() {
    char *env = getenv("SZ");
    if (!env) return;
    unsigned int len = (unsigned int)atoi(env);  // taint must survive cast
    char *buf = (char*)malloc(len);              // ERROR: tainted allocation size
    if (buf) free(buf);
}

// Variant 2: static_cast
void variant_static_cast() {
    char *env = getenv("SZ");
    if (!env) return;
    unsigned long len = static_cast<unsigned long>(atoi(env));  // taint must survive
    char *buf = (char*)malloc(len);                             // ERROR: tainted
    if (buf) free(buf);
}

int main() { return 0; }
