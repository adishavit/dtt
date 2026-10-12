// dtt-run: enforce
// 896: Relational path conditions - guards between expressions prove bounds
//
// A guard such as `n <= len` or `1 + 2 + payload + 16 > length` relates two
// symbolic values. Inside the guarded branch (or after an early return) that
// relation is a fact the solver may use. Every copy below is safe and must be
// proven, so this fixture runs in enforce mode.

#include <cstddef>
#include <cstdlib>
#include <cstring>

// Variant 1: then-branch, variable vs variable
void variant1_then(char *out, size_t len, size_t n) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    if (n <= len) {
        memcpy(out, data, n);
    }
    free(data);
}

// Variant 2: early return keeps the negation
void variant2_early_return(char *out, size_t len, size_t n) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    if (n > len) {
        free(data);
        return;
    }
    memcpy(out, data, n);
    free(data);
}

// Variant 3: reversed operands
void variant3_reversed(char *out, size_t len, size_t n) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    if (len >= n) memcpy(out, data, n);
    free(data);
}

// Variant 4: else-branch holds the negation
void variant4_else(char *out, size_t len, size_t n) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    if (n > len) {
        memcpy(out, data, len);
    } else {
        memcpy(out, data, n);
    }
    free(data);
}

// Variant 5: conjunction proves both bounds
void variant5_conjunction(size_t src_len, size_t copy_len) {
    char dst[64];
    char *src = static_cast<char *>(malloc(src_len));
    if (!src) return;
    if (copy_len <= src_len && copy_len <= sizeof(dst)) {
        memcpy(dst, src, copy_len);
    }
    free(src);
}

// Variant 6: disjunction with early return keeps both negations
void variant6_disjunction_return(size_t src_len, size_t copy_len) {
    char dst[64];
    char *src = static_cast<char *>(malloc(src_len));
    if (!src) return;
    if (copy_len > src_len || copy_len > sizeof(dst)) {
        free(src);
        return;
    }
    memcpy(dst, src, copy_len);
    free(src);
}

// Variant 7: offset pointer with an expression guard (Heartbleed fix shape)
void variant7_offset_expression(char *out, size_t length, size_t payload) {
    char *data = static_cast<char *>(malloc(length));
    if (!data) return;
    if (1 + 2 + 16 > length) { free(data); return; }
    char *p = data + 3;
    if (1 + 2 + payload + 16 > length) { free(data); return; }
    memcpy(out, p, payload);
    free(data);
}

// Variant 8: bound is a struct field
struct Record { size_t length; };
void variant8_field_bound(char *out, const Record *r, size_t n) {
    if (!r) return;
    char *data = static_cast<char *>(malloc(r->length));
    if (!data) return;
    if (n > r->length) { free(data); return; }
    memcpy(out, data, n);
    free(data);
}

// Variant 9: negated comparison
void variant9_not(char *out, size_t len, size_t n) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    if (!(n <= len)) { free(data); return; }
    memcpy(out, data, n);
    free(data);
}

// Variant 10: fact survives a loop that does not modify its variables
void variant10_unrelated_loop(char *out, size_t len, size_t n) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    if (n > len) { free(data); return; }
    size_t sum = 0;
    for (size_t i = 0; i < 4; ++i) sum += i;
    (void)sum;
    memcpy(out, data, n);
    free(data);
}

// Variant 11: nested guards combine
void variant11_nested(char *out, size_t len, size_t a, size_t b) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    if (a + b <= len) {
        if (b > 0) {
            memcpy(out, data, a);
        }
    }
    free(data);
}

int main() { return 0; }
