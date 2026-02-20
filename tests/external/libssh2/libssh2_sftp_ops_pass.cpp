// libssh2 SFTP operations -- correct usage patterns (should COMPILE)
//
// Tests that all SFTP operations satisfy DTT ghost field constraints when
// called on a properly authenticated session with valid SFTP/handle pointers.
// Covers: init, open, open_ex, read, write, close, close_handle, shutdown,
// stat, mkdir, rmdir, rename, unlink.

#include <libssh2.h>
#include <libssh2_sftp.h>

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

// Variant 1: SFTP file read workflow
//   sftp_init -> sftp_open -> sftp_read -> sftp_close -> sftp_shutdown
void variant_sftp_read() {
    LIBSSH2_SESSION *session = make_authed_session();
    LIBSSH2_SFTP *sftp = libssh2_sftp_init(session);
    char buf[4096];

    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_open(
        sftp, "/etc/hostname", LIBSSH2_FXF_READ, 0);
    (void)libssh2_sftp_read(handle, buf, sizeof(buf));
    (void)libssh2_sftp_close(handle);
    (void)libssh2_sftp_shutdown(sftp);

    teardown_session(session);
}

// Variant 2: SFTP file write workflow
//   sftp_init -> sftp_open -> sftp_write -> sftp_close -> sftp_shutdown
void variant_sftp_write() {
    LIBSSH2_SESSION *session = make_authed_session();
    LIBSSH2_SFTP *sftp = libssh2_sftp_init(session);

    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_open(
        sftp, "/tmp/output.txt",
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR);
    (void)libssh2_sftp_write(handle, "hello world\n", 12);
    (void)libssh2_sftp_close(handle);
    (void)libssh2_sftp_shutdown(sftp);

    teardown_session(session);
}

// Variant 3: SFTP stat on a path
//   sftp_init -> sftp_stat -> sftp_shutdown
void variant_sftp_stat() {
    LIBSSH2_SESSION *session = make_authed_session();
    LIBSSH2_SFTP *sftp = libssh2_sftp_init(session);
    LIBSSH2_SFTP_ATTRIBUTES attrs;

    (void)libssh2_sftp_stat(sftp, "/etc/hostname", &attrs);
    (void)libssh2_sftp_shutdown(sftp);

    teardown_session(session);
}

// Variant 4: SFTP directory operations (mkdir, rmdir)
//   sftp_init -> sftp_mkdir -> sftp_rmdir -> sftp_shutdown
void variant_sftp_mkdir_rmdir() {
    LIBSSH2_SESSION *session = make_authed_session();
    LIBSSH2_SFTP *sftp = libssh2_sftp_init(session);

    (void)libssh2_sftp_mkdir(sftp, "/tmp/testdir",
                             LIBSSH2_SFTP_S_IRWXU);
    (void)libssh2_sftp_rmdir(sftp, "/tmp/testdir");
    (void)libssh2_sftp_shutdown(sftp);

    teardown_session(session);
}

// Variant 5: SFTP rename and unlink
//   sftp_init -> sftp_rename -> sftp_unlink -> sftp_shutdown
void variant_sftp_rename_unlink() {
    LIBSSH2_SESSION *session = make_authed_session();
    LIBSSH2_SFTP *sftp = libssh2_sftp_init(session);

    (void)libssh2_sftp_rename(sftp, "/tmp/old.txt", "/tmp/new.txt");
    (void)libssh2_sftp_unlink(sftp, "/tmp/new.txt");
    (void)libssh2_sftp_shutdown(sftp);

    teardown_session(session);
}

// Variant 6: SFTP open_ex (direct _ex call) and close_handle
//   sftp_init -> sftp_open_ex -> sftp_read -> sftp_close_handle -> sftp_shutdown
void variant_sftp_open_ex() {
    LIBSSH2_SESSION *session = make_authed_session();
    LIBSSH2_SFTP *sftp = libssh2_sftp_init(session);
    char buf[1024];

    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_open_ex(
        sftp, "/etc/passwd", 11,
        LIBSSH2_FXF_READ, 0, LIBSSH2_SFTP_OPENFILE);
    (void)libssh2_sftp_read(handle, buf, sizeof(buf));
    (void)libssh2_sftp_close_handle(handle);
    (void)libssh2_sftp_shutdown(sftp);

    teardown_session(session);
}

// Variant 7: Full SFTP workflow with multiple file operations
//   sftp_init -> mkdir -> open(write) -> write -> close -> open(read) -> read ->
//   close -> stat -> unlink -> rmdir -> shutdown
void variant_sftp_full_workflow() {
    LIBSSH2_SESSION *session = make_authed_session();
    LIBSSH2_SFTP *sftp = libssh2_sftp_init(session);
    char buf[1024];
    LIBSSH2_SFTP_ATTRIBUTES attrs;

    (void)libssh2_sftp_mkdir(sftp, "/tmp/workdir",
                             LIBSSH2_SFTP_S_IRWXU);

    LIBSSH2_SFTP_HANDLE *wh = libssh2_sftp_open(
        sftp, "/tmp/workdir/data.bin",
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT,
        LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR);
    (void)libssh2_sftp_write(wh, "data", 4);
    (void)libssh2_sftp_close(wh);

    LIBSSH2_SFTP_HANDLE *rh = libssh2_sftp_open(
        sftp, "/tmp/workdir/data.bin", LIBSSH2_FXF_READ, 0);
    (void)libssh2_sftp_read(rh, buf, sizeof(buf));
    (void)libssh2_sftp_close(rh);

    (void)libssh2_sftp_stat(sftp, "/tmp/workdir/data.bin", &attrs);
    (void)libssh2_sftp_unlink(sftp, "/tmp/workdir/data.bin");
    (void)libssh2_sftp_rmdir(sftp, "/tmp/workdir");
    (void)libssh2_sftp_shutdown(sftp);

    teardown_session(session);
}

int main() {
    variant_sftp_read();
    variant_sftp_write();
    variant_sftp_stat();
    variant_sftp_mkdir_rmdir();
    variant_sftp_rename_unlink();
    variant_sftp_open_ex();
    variant_sftp_full_workflow();
    return 0;
}
