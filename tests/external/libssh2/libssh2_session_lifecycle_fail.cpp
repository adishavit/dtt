// libssh2 session lifecycle -- use after free / double free (should FAIL to compile)
//
// Tests that DTT catches operations on a freed session. After libssh2_session_free,
// @session_state == 0, but libssh2_session_handshake requires @session_state == 1.

#include <libssh2.h>

// Variant 1: handshake after free -- session already torn down
//   @session_state == 0 after free, but handshake requires @session_state == 1
void variant_handshake_after_free() {
    LIBSSH2_SESSION *session = libssh2_session_init();   // @session_state = 1
    libssh2_session_free(session);                       // ensures @session_state = 0

    // DTT ERROR: requires arg0.@session_state == 1, but @session_state == 0
    (void)libssh2_session_handshake(session, 0);
}

// Variant 2: double free -- session_free called twice
//   After first free, @session_state == 0 and _nonnull tracking should catch this
void variant_double_free() {
    LIBSSH2_SESSION *session = libssh2_session_init();   // @session_state = 1
    libssh2_session_free(session);                       // ensures @session_state = 0

    // DTT ERROR: requires arg0._nonnull != 0 (or @session_state violation)
    libssh2_session_free(session);
}

int main() {
    variant_handshake_after_free();
    variant_double_free();
    return 0;
}
