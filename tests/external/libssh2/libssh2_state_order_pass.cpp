// libssh2 full state sequences -- correct end-to-end ordering (should COMPILE)
//
// Tests complete libssh2 usage scenarios: session init, handshake, authentication,
// subsystem operations (channel/SFTP/SCP), and teardown. Validates that all ghost
// fields (@session_state, _nonnull) are satisfied at every API call boundary.

#include <libssh2.h>
#include <libssh2_sftp.h>
#include <sys/stat.h>

// Variant 1: Channel exec sequence
//   init -> handshake -> auth -> channel_open -> exec -> read -> close -> free -> disconnect -> free
void variant_channel_exec() {
    LIBSSH2_SESSION *session = libssh2_session_init();       // @session_state = 1

    (void)libssh2_session_handshake(session, 0);             // ensures @session_state = 2
    (void)libssh2_userauth_password(session, "user", "pw");  // ensures @session_state = 3

    LIBSSH2_CHANNEL *channel = libssh2_channel_open_session(session);  // requires @session_state == 3
    char buf[1024];

    (void)libssh2_channel_exec(channel, "ls -la");           // requires arg0._nonnull != 0
    (void)libssh2_channel_read(channel, buf, sizeof(buf));   // requires arg0._nonnull != 0
    (void)libssh2_channel_close(channel);                    // requires arg0._nonnull != 0
    (void)libssh2_channel_free(channel);                     // requires arg0._nonnull != 0

    (void)libssh2_session_disconnect(session, "done");       // ensures @session_state = 4
    libssh2_session_free(session);                           // ensures @session_state = 0
}

// Variant 2: SFTP file read sequence
//   init -> handshake -> auth -> sftp_init -> sftp_open -> sftp_read -> sftp_close ->
//   sftp_shutdown -> disconnect -> free
void variant_sftp_read() {
    LIBSSH2_SESSION *session = libssh2_session_init();       // @session_state = 1

    (void)libssh2_session_handshake(session, 0);             // ensures @session_state = 2
    (void)libssh2_userauth_password(session, "user", "pw");  // ensures @session_state = 3

    LIBSSH2_SFTP *sftp = libssh2_sftp_init(session);         // requires @session_state == 3
    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_open(         // requires arg0._nonnull != 0
        sftp, "/remote/file.txt", LIBSSH2_FXF_READ, 0);
    char buf[1024];

    (void)libssh2_sftp_read(handle, buf, sizeof(buf));       // requires arg0._nonnull != 0
    (void)libssh2_sftp_close(handle);                        // requires arg0._nonnull != 0
    (void)libssh2_sftp_shutdown(sftp);                       // requires arg0._nonnull != 0

    (void)libssh2_session_disconnect(session, "done");       // ensures @session_state = 4
    libssh2_session_free(session);                           // ensures @session_state = 0
}

// Variant 3: SCP receive sequence
//   init -> handshake -> auth -> scp_recv2 -> disconnect -> free
void variant_scp_recv() {
    LIBSSH2_SESSION *session = libssh2_session_init();       // @session_state = 1
    libssh2_struct_stat sb;

    (void)libssh2_session_handshake(session, 0);             // ensures @session_state = 2
    (void)libssh2_userauth_password(session, "user", "pw");  // ensures @session_state = 3

    LIBSSH2_CHANNEL *channel = libssh2_scp_recv2(            // requires @session_state == 3
        session, "/remote/file.txt", &sb);
    (void)channel;

    (void)libssh2_session_disconnect(session, "done");       // ensures @session_state = 4
    libssh2_session_free(session);                           // ensures @session_state = 0
}

int main() {
    variant_channel_exec();
    variant_sftp_read();
    variant_scp_recv();
    return 0;
}
