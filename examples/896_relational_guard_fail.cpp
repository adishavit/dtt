// 896: Relational path conditions - facts must not outlive their validity
//
// Each copy below is unsafe or unprovable because the guard no longer holds:
// a variable changed, control flow merged, or the guard does not imply the
// needed relation. DTT must still report each one.

#include <cstddef>
#include <cstdlib>
#include <cstring>

// Variant 1: no guard at all
void variant1_unguarded(char *out, size_t len, size_t n) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    memcpy(out, data, n);  // dtt-error {{over-read}}
    free(data);
}

// Variant 2: plain assignment after the guard
void variant2_assign(char *out, size_t len, size_t n) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    if (n > len) { free(data); return; }
    n = len + 5;
    memcpy(out, data, n);  // dtt-error {{over-read}}
    free(data);
}

// Variant 3: compound assignment after the guard
void variant3_compound(char *out, size_t len, size_t n) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    if (n > len) { free(data); return; }
    n += 5;
    memcpy(out, data, n);  // dtt-error {{over-read}}
    free(data);
}

// Variant 4: increment after the guard
void variant4_increment(char *out, size_t len, size_t n) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    if (n > len) { free(data); return; }
    n++;
    memcpy(out, data, n);  // dtt-error {{over-read}}
    free(data);
}

// Variant 5: variable modified inside a loop
void variant5_loop(char *out, size_t len, size_t n) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    if (n > len) { free(data); return; }
    for (int i = 0; i < 3; ++i) {
        memcpy(out, data, n);  // dtt-error {{over-read}}
        n = n * 2;
    }
    free(data);
}

// Variant 6: then-branch fact does not survive the merge
void variant6_merge(char *out, size_t len, size_t n) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    if (n <= len) {
        memcpy(out, data, 0);
    }
    memcpy(out, data, n);  // dtt-error {{over-read}}
    free(data);
}

// Variant 7: else-branch of an upper-bound guard
void variant7_else(char *out, size_t len, size_t n) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    if (n <= len) {
        memcpy(out, data, n);
    } else {
        memcpy(out, data, n);  // dtt-error {{over-read}}
    }
    free(data);
}

// Variant 8: disjunction gives no fact in its then-branch
void variant8_disjunction_then(char *out, size_t len, size_t n, bool force) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    if (n <= len || force) {
        memcpy(out, data, n);  // dtt-error {{over-read}}
    }
    free(data);
}

// Variant 9: guard checks the wrong relation (off by the offset)
void variant9_offset(char *out, size_t length, size_t payload) {
    char *data = static_cast<char *>(malloc(length));
    if (!data) return;
    if (3 > length) { free(data); return; }
    char *p = data + 3;
    if (payload > length) { free(data); return; }
    memcpy(out, p, payload);  // dtt-error {{over-read}}
    free(data);
}

// Variant 10: bound field may change in a call that receives the struct
struct Record { size_t length; };
void shrink(Record *r);
void variant10_call_invalidates(char *out, Record *r, size_t n) {
    if (!r) return;
    char *data = static_cast<char *>(malloc(r->length));
    if (!data) return;
    if (n > r->length) { free(data); return; }
    shrink(r);
    memcpy(out, data, n);  // dtt-error {{over-read}}
    free(data);
}

// Variant 11: fact from inside a loop does not escape it via break
void variant11_break(char *out, size_t len, size_t n) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    for (int i = 0; i < 2; ++i) {
        if (n > len) break;
    }
    memcpy(out, data, n);  // dtt-error {{over-read}}
    free(data);
}

// Variant 12: a label is a join point; the goto path skips the guard
void variant12_goto(char *out, size_t len, size_t n, bool skip) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    if (skip) goto copy;
    if (n > len) { free(data); return; }
copy:
    memcpy(out, data, n);  // dtt-error {{over-read}}
    free(data);
}

// Variant 13: variable written through a pointer
void variant14_alias(char *out, size_t len, size_t n) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    if (n > len) { free(data); return; }
    size_t *pn = &n;
    *pn = len + 9;
    memcpy(out, data, n);  // dtt-error {{over-read}}
    free(data);
}

int main() { return 0; }
