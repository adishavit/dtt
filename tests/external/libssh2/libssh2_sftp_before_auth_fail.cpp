// libssh2 SFTP operations before authentication -- wrong state (should FAIL to compile)
//
// Tests that DTT catches SFTP subsystem initialization and SCP send64 on an
// unauthenticated session. sftp_init requires @session_state == 3, scp_send64
// requires @session_state == 3, but after handshake @session_state == 2.

#include <libssh2.h>
#include <libssh2_sftp.h>

// Variant 1: sftp_init before authentication
//   @session_state == 2 (handshake complete), but sftp_init requires @session_state == 3
void variant_sftp_init_before_auth() {
    LIBSSH2_SESSION *session = libssh2_session_init();   // @session_state = 1
    (void)libssh2_session_handshake(session, 0);         // @session_state = 2

    // DTT ERROR: requires arg0.@session_state == 3, but @session_state == 2
    (void)libssh2_sftp_init(session);
}

// Variant 2: scp_send64 before authentication
//   @session_state == 2 (handshake complete), but scp_send64 requires @session_state == 3
void variant_scp_send64_before_auth() {
    LIBSSH2_SESSION *session = libssh2_session_init();   // @session_state = 1
    (void)libssh2_session_handshake(session, 0);         // @session_state = 2

    // DTT ERROR: requires arg0.@session_state == 3, but @session_state == 2
    (void)libssh2_scp_send64(session, "/remote/file.bin", 0644,
                             (libssh2_int64_t)1024, 0, 0);
}

// Variant 3: channel forwarding before authentication
//   @session_state == 2 (handshake complete), but forward_listen requires @session_state == 3
void variant_forward_before_auth() {
    LIBSSH2_SESSION *session = libssh2_session_init();   // @session_state = 1
    (void)libssh2_session_handshake(session, 0);         // @session_state = 2

    // DTT ERROR: requires arg0.@session_state == 3, but @session_state == 2
    (void)libssh2_channel_forward_listen(session, 8080);
}

// Variant 4: direct_tcpip before authentication
//   @session_state == 2 (handshake complete), but direct_tcpip requires @session_state == 3
void variant_direct_tcpip_before_auth() {
    LIBSSH2_SESSION *session = libssh2_session_init();   // @session_state = 1
    (void)libssh2_session_handshake(session, 0);         // @session_state = 2

    // DTT ERROR: requires arg0.@session_state == 3, but @session_state == 2
    (void)libssh2_channel_direct_tcpip(session, "host", 80);
}

int main() {
    variant_sftp_init_before_auth();
    variant_scp_send64_before_auth();
    variant_forward_before_auth();
    variant_direct_tcpip_before_auth();
    return 0;
}
