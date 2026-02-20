// libssh2 session lifecycle -- correct usage patterns (should COMPILE)
//
// Tests that the libssh2_session_init -> handshake -> auth -> disconnect -> free
// lifecycle is tracked correctly by DTT ghost field @session_state. Each variant
// follows a valid state progression through the session state machine:
//   0 (freed) -> 1 (init) -> 2 (handshake) -> 3 (auth) -> 4 (disconnected) -> 0 (freed)

#include <libssh2.h>

// Variant 1: Full lifecycle with password authentication
//   init -> handshake -> password_auth -> disconnect -> free
void variant_password_auth() {
    LIBSSH2_SESSION *session = libssh2_session_init();       // @session_state = 1
    int sock = 0;

    (void)libssh2_session_handshake(session, sock);          // requires @session_state == 1, ensures @session_state = 2
    (void)libssh2_userauth_password(session, "user", "pw");  // requires @session_state == 2, ensures @session_state = 3
    (void)libssh2_session_disconnect(session, "done");       // ensures @session_state = 4
    libssh2_session_free(session);                           // ensures @session_state = 0
}

// Variant 2: Full lifecycle with public-key authentication
//   init -> handshake -> pubkey_auth -> disconnect -> free
void variant_pubkey_auth() {
    LIBSSH2_SESSION *session = libssh2_session_init();       // @session_state = 1
    int sock = 0;

    (void)libssh2_session_handshake(session, sock);          // requires @session_state == 1, ensures @session_state = 2
    (void)libssh2_userauth_publickey_fromfile(               // requires @session_state == 2, ensures @session_state = 3
        session, "user", "/home/user/.ssh/id_rsa.pub",
        "/home/user/.ssh/id_rsa", "");
    (void)libssh2_session_disconnect(session, "done");       // ensures @session_state = 4
    libssh2_session_free(session);                           // ensures @session_state = 0
}

int main() {
    variant_password_auth();
    variant_pubkey_auth();
    return 0;
}
