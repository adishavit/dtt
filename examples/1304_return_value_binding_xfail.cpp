// 1304_return_value_binding_xfail.cpp
// Tests: Return value binding gaps — string content, three-valued returns, snprintf.
// These variants require API-specific models that DTT doesn't have yet.

#include <cstdlib>
#include <cstring>
#include <cstdio>

void send_data(const char* buf, size_t len);
void* process_buffer(void* buf, size_t len);

// =============================================================================
// VARIANT 1: strlen() return cached, string modified
// Gap: DTT doesn't track string content — NUL terminator insertion doesn't
// change _size_bytes, so the cached length isn't flagged stale.
// =============================================================================
void test_strlen_stale(size_t n) {
    char* s = (char*)malloc(n);
    if (!s) return;
    memset(s, 'A', n - 1);
    s[n - 1] = '\0';

    size_t len = n - 1;                // len tracks actual string length

    // Truncate the string
    if (n > 10) {
        s[5] = '\0';                   // String is now 5 chars
        // len is still n-1 — DESYNC!
    }

    send_data(s, len);                 // BUG: reads past NUL terminator
    free(s);
}

// =============================================================================
// VARIANT 2: EVP_VerifyFinal pattern — return encodes verification state
// Gap: Three-valued return (-1/0/1) misread as boolean. Requires API model
// for return value semantics (not a staleness issue).
// =============================================================================
int fake_verify(const void* sig, size_t siglen);

void test_verify_return_misread(const void* sig, size_t siglen) {
    int ret = fake_verify(sig, siglen);
    // ret == -1: error
    // ret ==  0: signature invalid
    // ret ==  1: signature valid

    if (ret) {  // BUG: treats -1 (error) as success
        // Proceed as if signature is valid
        process_buffer((void*)sig, siglen);
    }
}

// =============================================================================
// VARIANT 3: snprintf return is needed size — used as actual size
// Gap: snprintf returns chars that WOULD have been written, not actual written.
// Requires API model for snprintf's return semantics.
// =============================================================================
void test_snprintf_return_misuse(const char* name, int id) {
    char buf[64];
    int needed = snprintf(buf, sizeof(buf), "user_%s_%d", name, id);
    // If needed >= 64, output was truncated
    // But needed holds the FULL size, not the truncated size

    send_data(buf, needed);            // BUG: if truncated, reads past buf
}

int main() { return 0; }
