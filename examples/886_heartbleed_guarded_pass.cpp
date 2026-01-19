// 886_heartbleed_guarded_pass.cpp
// Tests: Heartbleed-style pattern WITH proper guard should pass
// From HEARTBLEED_AUTOTRACK_SPEC.md Test 2

#include <cstddef>
#include <cstring>
#include <cstdlib>

// Heartbleed pattern with proper bounds check
void test_heartbleed_like_guarded(size_t claimed_len) {
    unsigned char record[16];
    unsigned char* payload = record + 3;  // 13 bytes remain
    unsigned char* out = static_cast<unsigned char*>(malloc(claimed_len));
    if (!out) return;
    if (claimed_len <= sizeof(record) - 3) {
        memcpy(out, payload, claimed_len);  // OK: proven <= 13
    }
    free(out);
}

int main() { return 0; }
