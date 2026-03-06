// 1805_taint_heartbleed_fix_pass.cpp
// Heartbleed fix (OpenSSL 1.0.1g): bounds check on payload length before use.
// After validating payload <= record length, taint should be cleared.

extern "C" {
    void *malloc(unsigned long);
    void *memcpy(void*, const void*, unsigned long);
    void free(void*);
    int recv(int, void*, unsigned long, int);
}

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;

#define n2s(c,s) ((s=(((unsigned int)(c[0]))<< 8)| \
                      (((unsigned int)(c[1]))    )),c+=2)

struct ssl3_record_st {
    unsigned int length;
    uint8_t *data;
};

struct ssl3_state_st {
    ssl3_record_st rrec;
};

struct ssl_st {
    ssl3_state_st *s3;
};

#define TLS1_HB_REQUEST  1
#define TLS1_HB_RESPONSE 2

// Fixed version — verbatim logic from OpenSSL 1.0.1g fix
int dtls1_process_heartbeat_FIXED(ssl_st *s) {
    if (!s || !s->s3) return -1;  // satisfy DTT null checks

    // SSL record layer: data was received from the network (taint source)
    recv(0, s->s3->rrec.data, s->s3->rrec.length, 0);

    uint8_t *p = s->s3->rrec.data;
    unsigned int payload;
    uint8_t *pl;

    /* Read type and payload length first */
    if (1 + 2 + 16 > s->s3->rrec.length)
        return 0; /* silently discard */

    uint8_t hbtype = *p++;
    n2s(p, payload);

    // THE FIX: validate payload length against actual record length
    if (1 + 2 + payload + 16 > s->s3->rrec.length)
        return 0; /* silently discard per RFC 6520 sec. 4 */

    pl = p;

    if (hbtype == TLS1_HB_REQUEST) {
        uint8_t *buffer, *bp;
        unsigned int padding = 16;

        buffer = (uint8_t*)malloc(1 + 2 + payload + padding);  // OK: payload bounded
        if (!buffer) return -1;
        bp = buffer;

        *bp++ = TLS1_HB_RESPONSE;
        *bp++ = (uint8_t)((payload >> 8) & 0xff);
        *bp++ = (uint8_t)(payload & 0xff);

        memcpy(bp, pl, payload);                // OK: payload bounded by rrec.length

        free(buffer);
    }
    return 0;
}

int main() { return 0; }
