// libssh2 channel/SFTP/SCP before authentication -- wrong state order (should FAIL to compile)
//
// Tests that DTT catches channel, SFTP, and SCP operations before the session
// is authenticated. After handshake, @session_state == 2, but channel_open_session,
// sftp_init, and scp_recv all require @session_state == 3 (authenticated).

#include <libssh2.h>
#include <libssh2_sftp.h>

// Variant 1: channel_open_session before authentication
//   @session_state == 2 (handshake complete), but channel_open requires @session_state == 3
void variant_channel_before_auth() {
    LIBSSH2_SESSION *session = libssh2_session_init();       // @session_state = 1
    if (!session) return;

    (void)libssh2_session_handshake(session, 0);             // ensures @session_state = 2

    // DTT ERROR: requires arg0.@session_state == 3 (required state is not established in enforce mode)
    (void)libssh2_channel_open_session(session);  // dtt-error {{requires @session_state}}
}

// Variant 2: sftp_init before authentication
//   @session_state == 2 (handshake complete), but sftp_init requires @session_state == 3
void variant_sftp_before_auth() {
    LIBSSH2_SESSION *session = libssh2_session_init();       // @session_state = 1
    if (!session) return;

    (void)libssh2_session_handshake(session, 0);             // ensures @session_state = 2

    // DTT ERROR: requires arg0.@session_state == 3, but @session_state == 2
    (void)libssh2_sftp_init(session);  // dtt-error {{requires @session_state}}
}

// Variant 3: scp_recv before authentication
//   @session_state == 2 (handshake complete), but scp_recv requires @session_state == 3
void variant_scp_before_auth() {
    LIBSSH2_SESSION *session = libssh2_session_init();       // @session_state = 1
    if (!session) return;
    struct stat sb;

    (void)libssh2_session_handshake(session, 0);             // ensures @session_state = 2

    // DTT ERROR: requires arg0.@session_state == 3 (required state is not established in enforce mode)
    (void)libssh2_scp_recv(session, "/tmp/file.txt", &sb);  // dtt-error {{requires @session_state}}
}

int main() {
    variant_channel_before_auth();
    variant_sftp_before_auth();
    variant_scp_before_auth();
    return 0;
}
