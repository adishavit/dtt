// 885_heartbleed_overread_fail.cpp
// Tests: Heartbleed-style over-read detection (CWE-126)
// DTT catches source over-read even when destination is safe

#include <cstddef>
#include <cstring>
#include <cstdlib>

// Heartbleed pattern: large destination, small source, untrusted length
// Currently silent because dst is safe, src check is suppressed
void test_heartbleed_like(size_t claimed_len) {
    unsigned char record[16];
    unsigned char* payload = record + 3;  // 13 bytes remain
    unsigned char* out = static_cast<unsigned char*>(malloc(claimed_len));
    if (!out) return;
    memcpy(out, payload, claimed_len);  // SHOULD ERROR: src over-read unproven
    free(out);
}

int main() { return 0; }
