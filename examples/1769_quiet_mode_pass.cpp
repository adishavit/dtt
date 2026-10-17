// 1769: Quiet mode reports only proven findings. Run in quiet mode with
// -Werror: any diagnostic about something DTT cannot verify fails the test.

#include <cstring>
#include <cstdlib>

// Variant 1: a parameter of unknown nullness, dereferenced.
void variant1_unknown_nullness(int *p) { *p = 1; }

// Variant 2: a memory operation whose size cannot be verified.
void variant2_unverified_size(const char *src, size_t n) {
    char dst[8];
    memcpy(dst, src, n);
}

// Variant 3: a requirement the caller cannot be shown to meet.
struct Vec { int *data; int _size; };
[[clang::annotate("dtt:requires=_size > 0")]] void needsNonEmpty(Vec &v);
void variant3_unverified_requirement(Vec &v) { needsNonEmpty(v); }

// Variant 4: a buffer that may be too small for a callee's inferred size.
static void fill(char *b, size_t n) { memset(b, 0, n); }
void variant4_transitive(size_t n) {
    char buf[4];
    fill(buf, n);
}

// Variant 5: a static function's parameter that may be null at a call.
static int first(const int *p) { return *p; }
int variant5_callsite(const int *q) { return first(q); }

// Variant 6: a pointer freed on some paths only.
void variant6_maybe_freed(bool c) {
    char *p = static_cast<char *>(malloc(4));
    if (!p) return;
    if (c) free(p);
    if (!c) free(p);
}

// Variant 7: an unbounded formatting call, flagged as a risk, not a bug.
#include <cstdio>
void variant7_sprintf(int n) {
    char buf[16];
    sprintf(buf, "%d", n);
}

int main() { return 0; }
