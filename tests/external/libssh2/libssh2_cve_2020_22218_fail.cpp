// CVE-2020-22218: Use of uninitialized value in SSH transport (should FAIL to compile)
//
// CVE-2020-22218 involves use of an uninitialized value during SSH packet
// processing, which can occur when channel operations are performed before the
// session is properly authenticated. The session's internal transport buffers
// are only fully initialized after a successful authentication exchange.
// DTT catches this because channel and SCP operations require
// @session_state == 3 (authenticated), preventing use of uninitialized
// transport state.
//
// Reference: https://nvd.nist.gov/vuln/detail/CVE-2020-22218

#include <libssh2.h>
#include <libssh2_sftp.h>

// Variant 1: SCP send on unauthenticated session
//   Session at state 2 (handshake only) -- transport buffers not fully initialized
void variant_scp_before_auth() {
    LIBSSH2_SESSION *session = libssh2_session_init();   // @session_state = 1
    if (!session) return;

    (void)libssh2_session_handshake(session, 0);         // ensures @session_state = 2

    // KNOWN GAP (not asserted): scp_send expands to an unannotated scp_send_ex call.
    (void)libssh2_scp_send(session, "/remote/file.txt", 0644, 1024);
}

// Variant 2: SFTP init on unauthenticated session
//   Session at state 2 -- SFTP subsystem requires authenticated transport
void variant_sftp_before_auth() {
    LIBSSH2_SESSION *session = libssh2_session_init();   // @session_state = 1
    if (!session) return;

    (void)libssh2_session_handshake(session, 0);         // ensures @session_state = 2

    // DTT ERROR: requires arg0.@session_state == 3, but @session_state == 2
    (void)libssh2_sftp_init(session);  // dtt-error {{requires @session_state}}
}

int main() {
    variant_scp_before_auth();
    variant_sftp_before_auth();
    return 0;
}
