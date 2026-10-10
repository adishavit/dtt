// libssh2 method_pref after handshake -- wrong state (should FAIL to compile)
//
// Tests that DTT catches session_method_pref calls after the handshake.
// The .dtt annotation requires @session_state == 1 ("must set before handshake"),
// but after handshake @session_state == 2.

#include <libssh2.h>

// Variant 1: method_pref after handshake
//   @session_state == 2 (handshake complete), but method_pref requires @session_state == 1
void variant_method_pref_after_handshake() {
    LIBSSH2_SESSION *session = libssh2_session_init();   // @session_state = 1
    if (!session) return;
    (void)libssh2_session_handshake(session, 0);         // @session_state = 2

    // DTT ERROR: requires arg0.@session_state == 1 (required state is not established in enforce mode)
    (void)libssh2_session_method_pref(session, LIBSSH2_METHOD_KEX,  // dtt-error {{requires @session_state}}
                                       "diffie-hellman-group14-sha256");
}

// Variant 2: method_pref after authentication
//   @session_state == 3 (authenticated), but method_pref requires @session_state == 1
void variant_method_pref_after_auth() {
    LIBSSH2_SESSION *session = libssh2_session_init();       // @session_state = 1
    if (!session) return;
    (void)libssh2_session_handshake(session, 0);             // @session_state = 2
    (void)libssh2_userauth_password(session, "user", "pw");  // @session_state = 3

    // DTT ERROR: requires arg0.@session_state == 1 (required state is not established in enforce mode)
    (void)libssh2_session_method_pref(session, LIBSSH2_METHOD_HOSTKEY,  // dtt-error {{requires @session_state}}
                                       "ssh-ed25519");
}

int main() {
    variant_method_pref_after_handshake();
    variant_method_pref_after_auth();
    return 0;
}
