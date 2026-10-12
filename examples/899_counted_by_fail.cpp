// 899: counted_by - accesses beyond the counted extent are reported
//
// Also: a counted_by that does not name an integer field of the same record,
// or that is placed on a non-pointer field, is an annotation error.

#include <cstddef>
#include <cstdlib>
#include <cstring>

struct Buf {
    size_t len;
    [[clang::annotate("dtt:counted_by=len")]] char *data;
};

// Variant 1: unguarded copy
void variant1_copy(char *out, const Buf *b, size_t n) {
    if (!b || !b->data) return;
    memcpy(out, b->data, n);  // dtt-error {{over-read}}
}

// Variant 2: guard off by one
void variant2_off_by_one(char *out, const Buf *b, size_t n) {
    if (!b || !b->data) return;
    if (n > b->len + 1) return;
    memcpy(out, b->data, n);  // dtt-error {{over-read}}
}

// Variant 3: index equal to the count
char variant3_index_end(const Buf *b) {
    if (!b || !b->data) return 0;
    return b->data[b->len];  // dtt-error {{out of bounds}}
}

// Variant 4: Heartbleed (OpenSSL 1.0.1f tls1_process_heartbeat).
// Source side verbatim; response simplified as in 899_counted_by_pass.cpp.
struct SSL3_RECORD {
    int type;
    unsigned int length;
    [[clang::annotate("dtt:counted_by=length")]] unsigned char *data;
};
struct SSL3_STATE { SSL3_RECORD rrec; };
struct SSL { SSL3_STATE *s3; };
#define n2s(c, s) ((s = (((unsigned int)(c[0])) << 8) | (((unsigned int)(c[1])))), c += 2)

int variant4_heartbleed(SSL *s) {
    if (!s || !s->s3) return 0;
    unsigned char *p = &s->s3->rrec.data[0], *pl;
    unsigned short hbtype;
    unsigned int payload;
    hbtype = *p++;
    n2s(p, payload);  // dtt-error {{out of bounds}}
    pl = p;
    if (hbtype == 1) {
        unsigned char *buffer = static_cast<unsigned char *>(malloc(payload));
        if (!buffer) return 0;
        memcpy(buffer, pl, payload);  // dtt-error {{over-read}}
        free(buffer);
        return 1;
    }
    return 0;
}

// Variant 5: annotation errors
struct BadUnknown {
    size_t len;
    [[clang::annotate("dtt:counted_by=size")]] char *data;  // dtt-error {{counted_by}}
};
struct BadNotInteger {
    double len;
    [[clang::annotate("dtt:counted_by=len")]] char *data;  // dtt-error {{counted_by}}
};
struct BadNotPointer {
    size_t len;
    [[clang::annotate("dtt:counted_by=len")]] char data[8];  // dtt-error {{counted_by}}
};

int main() { return 0; }
