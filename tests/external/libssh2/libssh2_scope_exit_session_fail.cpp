// libssh2 scope_exit -- session not freed before scope exit (should FAIL to compile)
//
// Tests that DTT catches sessions that leak (not freed before the end of scope).
// libssh2_session_init has scope_exit requiring @session_state == 0, meaning
// the session must be freed before leaving the function. Forgetting to call
// libssh2_session_free is a resource leak.

#include <libssh2.h>

// Variant 1: session initialized but never freed
//   scope_exit requires @session_state == 0, but session is at state 1
void variant_init_no_free() {
    LIBSSH2_SESSION *session = libssh2_session_init();   // @session_state = 1
    (void)session;
    // DTT ERROR: scope_exit requires @session_state == 0 at end of scope
}

// Variant 2: session fully used but not freed (missing session_free)
//   scope_exit requires @session_state == 0, but session is at state 4
void variant_disconnect_no_free() {
    LIBSSH2_SESSION *session = libssh2_session_init();       // @session_state = 1
    (void)libssh2_session_handshake(session, 0);             // @session_state = 2
    (void)libssh2_userauth_password(session, "user", "pw");  // @session_state = 3
    (void)libssh2_session_disconnect(session, "done");       // @session_state = 4
    // DTT ERROR: scope_exit requires @session_state == 0, but @session_state == 4
}

int main() {
    variant_init_no_free();
    variant_disconnect_no_free();
    return 0;
}
