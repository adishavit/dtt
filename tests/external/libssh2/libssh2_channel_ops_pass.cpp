// libssh2 channel operations -- correct usage patterns (should COMPILE)
//
// Tests that all channel operations satisfy DTT ghost field constraints when
// called on a properly authenticated session (@session_state == 3) with valid
// channel pointers. Covers: exec, shell, subsystem, read, write, eof,
// send_eof, wait_eof, close, wait_closed, free, set_blocking,
// get_exit_status, setenv, request_pty, process_startup.

#include <libssh2.h>

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

// Variant 1: channel exec with full read/write cycle and graceful shutdown
//   open -> setenv -> request_pty -> exec -> write -> read -> eof ->
//   send_eof -> wait_eof -> close -> wait_closed -> get_exit_status -> free
void variant_exec_full_cycle() {
    LIBSSH2_SESSION *session = make_authed_session();
    LIBSSH2_CHANNEL *ch = libssh2_channel_open_session(session);
    char buf[1024];

    (void)libssh2_channel_setenv(ch, "LANG", "en_US.UTF-8");
    (void)libssh2_channel_request_pty(ch, "xterm");
    (void)libssh2_channel_exec(ch, "ls -la");
    (void)libssh2_channel_write(ch, "input\n", 6);
    (void)libssh2_channel_read(ch, buf, sizeof(buf));
    (void)libssh2_channel_eof(ch);
    (void)libssh2_channel_send_eof(ch);
    (void)libssh2_channel_wait_eof(ch);
    (void)libssh2_channel_close(ch);
    (void)libssh2_channel_wait_closed(ch);
    (void)libssh2_channel_get_exit_status(ch);
    (void)libssh2_channel_free(ch);

    teardown_session(session);
}

// Variant 2: interactive shell with pty and blocking mode control
//   open -> set_blocking -> request_pty -> shell -> write -> read -> close -> free
void variant_shell_interactive() {
    LIBSSH2_SESSION *session = make_authed_session();
    LIBSSH2_CHANNEL *ch = libssh2_channel_open_session(session);
    char buf[1024];

    libssh2_channel_set_blocking(ch, 0);
    (void)libssh2_channel_request_pty(ch, "vt100");
    (void)libssh2_channel_shell(ch);
    (void)libssh2_channel_write(ch, "exit\n", 5);
    (void)libssh2_channel_read(ch, buf, sizeof(buf));
    (void)libssh2_channel_close(ch);
    (void)libssh2_channel_free(ch);

    teardown_session(session);
}

// Variant 3: subsystem (e.g. "sftp") channel
//   open -> subsystem -> read -> close -> free
void variant_subsystem() {
    LIBSSH2_SESSION *session = make_authed_session();
    LIBSSH2_CHANNEL *ch = libssh2_channel_open_session(session);
    char buf[1024];

    (void)libssh2_channel_subsystem(ch, "sftp");
    (void)libssh2_channel_read(ch, buf, sizeof(buf));
    (void)libssh2_channel_close(ch);
    (void)libssh2_channel_free(ch);

    teardown_session(session);
}

// Variant 4: multiple channels on the same session
//   open ch1 -> exec -> open ch2 -> exec -> read ch1 -> close ch1 -> free ch1 ->
//   read ch2 -> close ch2 -> free ch2
void variant_multiple_channels() {
    LIBSSH2_SESSION *session = make_authed_session();
    char buf[1024];

    LIBSSH2_CHANNEL *ch1 = libssh2_channel_open_session(session);
    (void)libssh2_channel_exec(ch1, "uptime");

    LIBSSH2_CHANNEL *ch2 = libssh2_channel_open_session(session);
    (void)libssh2_channel_exec(ch2, "hostname");

    (void)libssh2_channel_read(ch1, buf, sizeof(buf));
    (void)libssh2_channel_close(ch1);
    (void)libssh2_channel_free(ch1);

    (void)libssh2_channel_read(ch2, buf, sizeof(buf));
    (void)libssh2_channel_close(ch2);
    (void)libssh2_channel_free(ch2);

    teardown_session(session);
}

// Variant 5: channel_open_ex (direct _ex call instead of macro)
//   open_ex -> exec -> close -> free
void variant_open_ex() {
    LIBSSH2_SESSION *session = make_authed_session();
    LIBSSH2_CHANNEL *ch = libssh2_channel_open_ex(
        session, "session", sizeof("session") - 1,
        LIBSSH2_CHANNEL_WINDOW_DEFAULT,
        LIBSSH2_CHANNEL_PACKET_DEFAULT, NULL, 0);

    (void)libssh2_channel_exec(ch, "whoami");
    (void)libssh2_channel_close(ch);
    (void)libssh2_channel_free(ch);

    teardown_session(session);
}

// Variant 6: process_startup direct call (underlying function for exec/shell/subsystem)
//   open -> process_startup -> close -> free
void variant_process_startup() {
    LIBSSH2_SESSION *session = make_authed_session();
    LIBSSH2_CHANNEL *ch = libssh2_channel_open_session(session);

    (void)libssh2_channel_process_startup(ch, "exec", 4, "date", 4);
    (void)libssh2_channel_close(ch);
    (void)libssh2_channel_free(ch);

    teardown_session(session);
}

// Variant 7: channel_read_ex and channel_write_ex (direct _ex calls)
//   open -> exec -> write_ex -> read_ex -> close -> free
void variant_read_write_ex() {
    LIBSSH2_SESSION *session = make_authed_session();
    LIBSSH2_CHANNEL *ch = libssh2_channel_open_session(session);
    char buf[1024];

    (void)libssh2_channel_exec(ch, "cat");
    (void)libssh2_channel_write_ex(ch, 0, "hello", 5);
    (void)libssh2_channel_read_ex(ch, 0, buf, sizeof(buf));
    (void)libssh2_channel_close(ch);
    (void)libssh2_channel_free(ch);

    teardown_session(session);
}

// Variant 8: request_pty_ex (direct _ex call with explicit dimensions)
//   open -> request_pty_ex -> shell -> close -> free
void variant_request_pty_ex() {
    LIBSSH2_SESSION *session = make_authed_session();
    LIBSSH2_CHANNEL *ch = libssh2_channel_open_session(session);

    (void)libssh2_channel_request_pty_ex(ch, "xterm-256color", 14,
                                         NULL, 0, 120, 40, 0, 0);
    (void)libssh2_channel_shell(ch);
    (void)libssh2_channel_close(ch);
    (void)libssh2_channel_free(ch);

    teardown_session(session);
}

// Variant 9: setenv_ex (direct _ex call)
//   open -> setenv_ex -> exec -> close -> free
void variant_setenv_ex() {
    LIBSSH2_SESSION *session = make_authed_session();
    LIBSSH2_CHANNEL *ch = libssh2_channel_open_session(session);

    (void)libssh2_channel_setenv_ex(ch, "PATH", 4, "/usr/bin", 8);
    (void)libssh2_channel_exec(ch, "echo $PATH");
    (void)libssh2_channel_close(ch);
    (void)libssh2_channel_free(ch);

    teardown_session(session);
}

int main() {
    variant_exec_full_cycle();
    variant_shell_interactive();
    variant_subsystem();
    variant_multiple_channels();
    variant_open_ex();
    variant_process_startup();
    variant_read_write_ex();
    variant_request_pty_ex();
    variant_setenv_ex();
    return 0;
}
