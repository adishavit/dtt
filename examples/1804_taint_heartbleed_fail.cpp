// 1804_taint_heartbleed_fail.cpp
// Heartbleed (CVE-2014-0160): verbatim vulnerable pattern from OpenSSL ssl/d1_both.c
// dtls1_process_heartbeat() reads payload length from untrusted packet
// and uses it in memcpy without bounds checking.

extern "C" {
    void *malloc(unsigned long);
    void *memcpy(void*, const void*, unsigned long);
    void free(void*);
    int recv(int, void*, unsigned long, int);
}

// Simulate OpenSSL macros/types
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;

// n2s: network-to-short (big-endian 2-byte read), from OpenSSL ssl/ssl_locl.h
#define n2s(c,s) ((s=(((unsigned int)(c[0]))<< 8)| \
                      (((unsigned int)(c[1]))    )),c+=2)

// Simplified record structure matching OpenSSL's SSL3_RECORD
struct ssl3_record_st {
    unsigned int length;   // actual received record length
    uint8_t *data;         // received data
};

struct ssl3_state_st {
    ssl3_record_st rrec;
};

struct ssl_st {
    ssl3_state_st *s3;
};

#define TLS1_HB_REQUEST  1
#define TLS1_HB_RESPONSE 2

// Vulnerable version — verbatim logic from OpenSSL 1.0.1 through 1.0.1f
int dtls1_process_heartbeat_VULNERABLE(ssl_st *s) {
    if (!s || !s->s3) return -1;  // satisfy DTT null checks

    // SSL record layer: data was received from the network (taint source)
    recv(0, s->s3->rrec.data, s->s3->rrec.length, 0);

    uint8_t *p = s->s3->rrec.data;
    unsigned int payload;
    uint8_t *pl;

    /* Read type and payload length first */
    uint8_t hbtype = *p++;
    n2s(p, payload);                            // payload from UNTRUSTED packet
    pl = p;

    if (hbtype == TLS1_HB_REQUEST) {
        uint8_t *buffer, *bp;
        unsigned int padding = 16; /* Use minimum padding */

        /* Allocate memory for the response */
        buffer = (uint8_t*)malloc(1 + 2 + payload + padding);  // tainted alloc size
        if (!buffer) return -1;
        bp = buffer;

        /* Enter response type and length */
        *bp++ = TLS1_HB_RESPONSE;
        *bp++ = (uint8_t)((payload >> 8) & 0xff);
        *bp++ = (uint8_t)(payload & 0xff);

        /* Copy payload from request to response — THE BUG */
        memcpy(bp, pl, payload);                // tainted copy length — reads OOB

        free(buffer);
    }
    return 0;
}

int main() { return 0; }
