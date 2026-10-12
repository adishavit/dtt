// 895: Heartbleed (CVE-2014-0160) in its real shape - DTT gap
// Reduced from OpenSSL 1.0.1f ssl/t1_lib.c tls1_process_heartbeat().
//
// 885_heartbleed_overread_fail.cpp catches the over-read only because the
// source is a local array of known size. In OpenSSL the source points into
// s->s3->rrec.data, a struct-field pointer allocated elsewhere, whose length
// lives in the sibling field rrec.length. DTT has no extent for such pointers,
// so memcpy is untracked (Category A) and silent even in enforce mode.
//
// Missing pieces:
//   - an extent for untracked pointers (field relation data <-> length, or a
//     function requires= on _size_bytes used as an assumption in the body)
//   - a diagnostic for memcpy with an untracked source extent in enforce mode
//
// The response buffer is simplified to malloc(payload) so that only the
// source over-read remains; the original s2n() header writes trigger an
// unrelated bounds diagnostic.

#include <cstdlib>
#include <cstring>

struct SSL3_RECORD {
    int type;
    unsigned int length;
    unsigned char *data;
};
struct SSL3_STATE { SSL3_RECORD rrec; };
struct SSL { SSL3_STATE *s3; };

#define n2s(c, s) ((s = (((unsigned int)(c[0])) << 8) | (((unsigned int)(c[1])))), c += 2)

// Variant 1: OpenSSL shape - source is a struct-field pointer
int variant1_openssl_shape(SSL *s) {
    if (!s || !s->s3) return 0;
    unsigned char *p = &s->s3->rrec.data[0], *pl;
    unsigned short hbtype;
    unsigned int payload;
    hbtype = *p++;
    n2s(p, payload);
    pl = p;
    if (hbtype == 1) {
        unsigned char *buffer = static_cast<unsigned char *>(malloc(payload));
        if (!buffer) return 0;
        memcpy(buffer, pl, payload);  // BUG: payload never checked against rrec.length
        free(buffer);
        return 1;
    }
    return 0;
}

// Variant 2: record passed as pointer + length parameters, no annotation
int variant2_param_buffer(unsigned char *data, unsigned int length) {
    unsigned char *p = data;
    unsigned int payload;
    if (length < 3) return 0;
    p++;
    n2s(p, payload);
    unsigned char *out = static_cast<unsigned char *>(malloc(payload));
    if (!out) return 0;
    memcpy(out, p, payload);  // BUG: payload may exceed length - 3
    free(out);
    return 1;
}

// Variant 3: same, with the extent stated as a precondition.
// The requires= is not used as an assumption inside the body.
[[clang::annotate("dtt:requires=data._size_bytes >= length")]]
int variant3_param_buffer_annotated(unsigned char *data, unsigned int length) {
    unsigned char *p = data;
    unsigned int payload;
    if (length < 3) return 0;
    p++;
    n2s(p, payload);
    unsigned char *out = static_cast<unsigned char *>(malloc(payload));
    if (!out) return 0;
    memcpy(out, p, payload);  // BUG: payload may exceed length - 3
    free(out);
    return 1;
}

// Variant 4: direct offset from an annotated parameter, caller-supplied length
[[clang::annotate("dtt:requires=data._size_bytes >= length")]]
void variant4_offset_annotated(unsigned char *out, unsigned char *data,
                               unsigned int length, unsigned int payload) {
    if (length < 3) return;
    memcpy(out, data + 3, payload);  // BUG: payload may exceed length - 3
}

int main() { return 0; }
