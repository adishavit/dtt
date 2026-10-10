// libssh2 hostkey retrieval before handshake -- wrong state (should FAIL to compile)
//
// Tests that DTT catches session_hostkey calls before the handshake completes.
// The .dtt annotation requires @session_state >= 2 ("handshake not complete"),
// but after init @session_state == 1.

#include <libssh2.h>

// Variant 1: hostkey query on freshly initialized session
//   @session_state == 1 (initialized), but session_hostkey requires @session_state >= 2
void variant_hostkey_before_handshake() {
    LIBSSH2_SESSION *session = libssh2_session_init();   // @session_state = 1
    if (!session) return;

    size_t len;
    int type;
    // DTT ERROR: requires arg0.@session_state >= 2 (required state is not established in enforce mode)
    const char *key = libssh2_session_hostkey(session, &len, &type);  // dtt-error {{requires @session_state}}
    (void)key;
}

int main() {
    variant_hostkey_before_handshake();
    return 0;
}
