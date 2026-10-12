// 1737: Index into a buffer sized by an unsigned expression - false positive
//
// The buffer holds 3 + payload >= 3 bytes, so bp[0] and bp[1] (bp = buf + 1)
// are in bounds. The index check's solver query does not know that unsigned
// values are non-negative, so it finds payload < 0 as a counterexample.
// (OpenSSL's s2n() writes into the heartbeat response have this shape.)
//
// Expected fix: constrain variables of unsigned type to be >= 0 in index
// bounds queries. Rename to _pass when DTT accepts this file.

#include <cstddef>
#include <cstdlib>

// Variant 1: s2n()-style header write after a one-byte type field
void variant1_s2n_shape(unsigned int payload) {
    unsigned char *buf = static_cast<unsigned char *>(malloc(3 + payload));
    if (!buf) return;
    unsigned char *bp = buf;
    *bp++ = 1;
    bp[0] = 0;
    bp[1] = 0;
    free(buf);
}

// Variant 2: size_t-sized buffer, constant index below the constant part
void variant2_size_t(size_t n) {
    char *buf = static_cast<char *>(malloc(n + 2));
    if (!buf) return;
    buf[1] = 0;
    free(buf);
}

int main() { return 0; }
