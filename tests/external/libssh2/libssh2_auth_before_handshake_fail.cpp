// libssh2 authentication before handshake -- wrong state order (should FAIL to compile)
//
// Tests that DTT catches authentication calls before the TLS/SSH handshake
// completes. After libssh2_session_init, @session_state == 1, but
// libssh2_userauth_password and libssh2_userauth_publickey_fromfile both
// require @session_state == 2 (handshake complete).

#include <libssh2.h>

// Variant 1: password auth before handshake
//   @session_state == 1 (initialized), but userauth_password requires @session_state == 2
void variant_password_before_handshake() {
    LIBSSH2_SESSION *session = libssh2_session_init();       // @session_state = 1
    if (!session) return;

    // DTT ERROR: requires arg0.@session_state == 2 (required state is not established in enforce mode)
    (void)libssh2_userauth_password(session, "user", "pw");  // dtt-error {{requires @session_state}}
}

// Variant 2: public-key auth before handshake
//   @session_state == 1 (initialized), but userauth_publickey_fromfile requires @session_state == 2
void variant_pubkey_before_handshake() {
    LIBSSH2_SESSION *session = libssh2_session_init();       // @session_state = 1
    if (!session) return;

    // DTT ERROR: requires arg0.@session_state == 2 (required state is not established in enforce mode)
    (void)libssh2_userauth_publickey_fromfile(  // dtt-error {{requires @session_state}}
        session, "user", "/home/user/.ssh/id_rsa.pub",
        "/home/user/.ssh/id_rsa", "");
}

int main() {
    variant_password_before_handshake();
    variant_pubkey_before_handshake();
    return 0;
}
