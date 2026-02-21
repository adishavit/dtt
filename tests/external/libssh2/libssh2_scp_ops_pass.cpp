// libssh2 SCP operations -- correct usage patterns (should COMPILE)
//
// Tests that all SCP operations satisfy DTT ghost field constraints when
// called on a properly authenticated session (@session_state == 3).
// Covers: scp_recv, scp_recv2, scp_send, scp_send64.

#include <libssh2.h>
#include <sys/stat.h>

// Helper: bring a session to authenticated state (state 3)
static LIBSSH2_SESSION *make_authed_session() {
    LIBSSH2_SESSION *session = libssh2_session_init();
    (void)libssh2_session_handshake(session, 0);
    (void)libssh2_userauth_password(session, "user", "pw");
    return session;
}

// Helper: tear down session from authenticated state
static void teardown_session(LIBSSH2_SESSION *session) {
    (void)libssh2_session_disconnect(session, "done");
    libssh2_session_free(session);
}

// Variant 1: SCP receive (deprecated scp_recv)
//   scp_recv -> channel_read -> channel_close -> channel_free
void variant_scp_recv() {
    LIBSSH2_SESSION *session = make_authed_session();
    struct stat sb;
    char buf[1024];

    LIBSSH2_CHANNEL *ch = libssh2_scp_recv(session, "/remote/file.dat", &sb);
    (void)libssh2_channel_read(ch, buf, sizeof(buf));
    (void)libssh2_channel_close(ch);
    (void)libssh2_channel_free(ch);

    teardown_session(session);
}

// Variant 2: SCP receive v2 (recommended scp_recv2)
//   scp_recv2 -> channel_read -> channel_close -> channel_free
void variant_scp_recv2() {
    LIBSSH2_SESSION *session = make_authed_session();
    libssh2_struct_stat sb;
    char buf[1024];

    LIBSSH2_CHANNEL *ch = libssh2_scp_recv2(session, "/remote/bigfile.bin", &sb);
    (void)libssh2_channel_read(ch, buf, sizeof(buf));
    (void)libssh2_channel_close(ch);
    (void)libssh2_channel_free(ch);

    teardown_session(session);
}

// Variant 3: SCP send (macro expands to scp_send_ex)
//   scp_send -> channel_write -> channel_send_eof -> channel_close -> channel_free
void variant_scp_send() {
    LIBSSH2_SESSION *session = make_authed_session();

    LIBSSH2_CHANNEL *ch = libssh2_scp_send(session, "/remote/upload.txt", 0644, 1024);
    (void)libssh2_channel_write(ch, "file content here", 17);
    (void)libssh2_channel_send_eof(ch);
    (void)libssh2_channel_close(ch);
    (void)libssh2_channel_free(ch);

    teardown_session(session);
}

// Variant 4: SCP send64 (large file support)
//   scp_send64 -> channel_write -> channel_send_eof -> channel_close -> channel_free
void variant_scp_send64() {
    LIBSSH2_SESSION *session = make_authed_session();

    LIBSSH2_CHANNEL *ch = libssh2_scp_send64(
        session, "/remote/largefile.bin", 0644,
        (libssh2_int64_t)5000000000LL, 0, 0);
    (void)libssh2_channel_write(ch, "data", 4);
    (void)libssh2_channel_send_eof(ch);
    (void)libssh2_channel_close(ch);
    (void)libssh2_channel_free(ch);

    teardown_session(session);
}

// Variant 5: SCP recv and send in the same session
//   scp_recv2 -> read -> close -> free -> scp_send -> write -> close -> free
void variant_scp_recv_then_send() {
    LIBSSH2_SESSION *session = make_authed_session();
    libssh2_struct_stat sb;
    char buf[1024];

    LIBSSH2_CHANNEL *rch = libssh2_scp_recv2(session, "/remote/source.txt", &sb);
    (void)libssh2_channel_read(rch, buf, sizeof(buf));
    (void)libssh2_channel_close(rch);
    (void)libssh2_channel_free(rch);

    LIBSSH2_CHANNEL *sch = libssh2_scp_send(session, "/remote/dest.txt", 0644, 512);
    (void)libssh2_channel_write(sch, buf, 512);
    (void)libssh2_channel_send_eof(sch);
    (void)libssh2_channel_close(sch);
    (void)libssh2_channel_free(sch);

    teardown_session(session);
}

int main() {
    variant_scp_recv();
    variant_scp_recv2();
    variant_scp_send();
    variant_scp_send64();
    variant_scp_recv_then_send();
    return 0;
}
