// CVE-2019-3855: Integer overflow in SSH transport (should FAIL to compile)
//
// CVE-2019-3855 is an integer overflow in the SSH packet transport layer
// that can be triggered by a malicious server during an authenticated session.
// The root cause is channel operations processed on a session that has not
// completed the full authentication handshake. DTT catches this because
// libssh2_channel_open_session requires @session_state == 3 (authenticated),
// preventing channel use on incompletely initialized sessions.
//
// Reference: https://nvd.nist.gov/vuln/detail/CVE-2019-3855

#include <libssh2.h>

// Variant 1: channel operations on session with only handshake (no auth)
//   Session at state 2 (handshake) -- attacker-controlled server could exploit
//   integer overflow in packet handling during premature channel operations
void variant_channel_before_auth() {
    LIBSSH2_SESSION *session = libssh2_session_init();   // @session_state = 1

    (void)libssh2_session_handshake(session, 0);         // ensures @session_state = 2

    // DTT ERROR: requires arg0.@session_state == 3, but @session_state == 2
    (void)libssh2_channel_open_session(session);
}

int main() {
    variant_channel_before_auth();
    return 0;
}
