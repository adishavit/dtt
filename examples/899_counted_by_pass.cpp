// dtt-run: enforce
// 899: counted_by - a pointer field's extent is a sibling field
//
// [[clang::annotate("dtt:counted_by=len")]] on a pointer field states that
// it points to at least `len` elements (`len` is a field of the same
// object). Accesses and copies through the field are then checked against
// that count. DTT_COUNTED_BY(len) in include/dtt.h expands to the attribute.

#include <cstddef>
#include <cstdlib>
#include <cstring>

struct Buf {
    size_t len;
    [[clang::annotate("dtt:counted_by=len")]] char *data;
};

// Variant 1: guarded copy straight from the field
void variant1_copy(char *out, const Buf *b, size_t n) {
    if (!b || !b->data) return;
    if (n > b->len) return;
    memcpy(out, b->data, n);
}

// Variant 2: guarded index
char variant2_index(const Buf *b, size_t i) {
    if (!b || !b->data) return 0;
    if (i < b->len) return b->data[i];
    return 0;
}

// Variant 3: pointer derived from the field keeps the count
void variant3_derived(char *out, const Buf *b) {
    if (!b || !b->data) return;
    if (b->len < 4) return;
    const char *p = &b->data[1];
    memcpy(out, p, b->len - 1);
}

// Variant 4: Heartbleed fixed (OpenSSL 1.0.1g tls1_process_heartbeat).
// Source side verbatim; the response is simplified to a single allocation
// (the original s2n() header writes hit an unrelated known false positive).
struct SSL3_RECORD {
    int type;
    unsigned int length;
    [[clang::annotate("dtt:counted_by=length")]] unsigned char *data;
};
struct SSL3_STATE { SSL3_RECORD rrec; };
struct SSL { SSL3_STATE *s3; };
#define n2s(c, s) ((s = (((unsigned int)(c[0])) << 8) | (((unsigned int)(c[1])))), c += 2)

int variant4_heartbeat_fixed(SSL *s) {
    if (!s || !s->s3) return 0;
    unsigned char *p = &s->s3->rrec.data[0], *pl;
    unsigned short hbtype;
    unsigned int payload;
    if (1 + 2 + 16 > s->s3->rrec.length) return 0;
    hbtype = *p++;
    n2s(p, payload);
    if (1 + 2 + payload + 16 > s->s3->rrec.length) return 0;
    pl = p;
    if (hbtype == 1) {
        unsigned char *buffer = static_cast<unsigned char *>(malloc(payload));
        if (!buffer) return 0;
        memcpy(buffer, pl, payload);
        free(buffer);
        return 1;
    }
    return 0;
}

int main() { return 0; }
