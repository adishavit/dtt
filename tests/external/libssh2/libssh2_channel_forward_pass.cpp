// libssh2 channel forwarding -- correct usage patterns (should COMPILE)
//
// Tests that all channel forwarding operations satisfy DTT ghost field
// constraints. Forward operations require an authenticated session
// (@session_state == 3); accept/cancel require a valid listener pointer.
// Covers: forward_listen, forward_listen_ex, forward_accept, forward_cancel,
// direct_tcpip, direct_tcpip_ex.

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

// Variant 1: forward_listen (macro) and forward_accept, then cancel
//   forward_listen -> forward_accept -> channel_close -> channel_free -> forward_cancel
void variant_forward_listen_accept() {
    LIBSSH2_SESSION *session = make_authed_session();

    LIBSSH2_LISTENER *listener = libssh2_channel_forward_listen(session, 8080);
    LIBSSH2_CHANNEL *ch = libssh2_channel_forward_accept(listener);

    (void)libssh2_channel_close(ch);
    (void)libssh2_channel_free(ch);
    (void)libssh2_channel_forward_cancel(listener);

    teardown_session(session);
}

// Variant 2: forward_listen_ex (direct _ex call with bound port)
//   forward_listen_ex -> forward_accept -> channel_read -> channel_close ->
//   channel_free -> forward_cancel
void variant_forward_listen_ex() {
    LIBSSH2_SESSION *session = make_authed_session();
    int bound_port = 0;
    char buf[1024];

    LIBSSH2_LISTENER *listener = libssh2_channel_forward_listen_ex(
        session, "0.0.0.0", 0, &bound_port, 16);
    LIBSSH2_CHANNEL *ch = libssh2_channel_forward_accept(listener);

    (void)libssh2_channel_read(ch, buf, sizeof(buf));
    (void)libssh2_channel_close(ch);
    (void)libssh2_channel_free(ch);
    (void)libssh2_channel_forward_cancel(listener);

    teardown_session(session);
}

// Variant 3: direct TCP/IP tunneling (macro)
//   direct_tcpip -> channel_write -> channel_read -> channel_close -> channel_free
void variant_direct_tcpip() {
    LIBSSH2_SESSION *session = make_authed_session();
    char buf[1024];

    LIBSSH2_CHANNEL *ch = libssh2_channel_direct_tcpip(
        session, "db.internal", 5432);
    (void)libssh2_channel_write(ch, "SELECT 1;\n", 10);
    (void)libssh2_channel_read(ch, buf, sizeof(buf));
    (void)libssh2_channel_close(ch);
    (void)libssh2_channel_free(ch);

    teardown_session(session);
}

// Variant 4: direct_tcpip_ex (direct _ex call with source host/port)
//   direct_tcpip_ex -> channel_write -> channel_close -> channel_free
void variant_direct_tcpip_ex() {
    LIBSSH2_SESSION *session = make_authed_session();

    LIBSSH2_CHANNEL *ch = libssh2_channel_direct_tcpip_ex(
        session, "web.internal", 80, "127.0.0.1", 12345);
    (void)libssh2_channel_write(ch, "GET / HTTP/1.0\r\n\r\n", 18);
    (void)libssh2_channel_close(ch);
    (void)libssh2_channel_free(ch);

    teardown_session(session);
}

// Variant 5: multiple forward listeners on same session
//   forward_listen(8080) -> forward_listen(9090) -> cancel(1) -> cancel(2)
void variant_multiple_listeners() {
    LIBSSH2_SESSION *session = make_authed_session();

    LIBSSH2_LISTENER *l1 = libssh2_channel_forward_listen(session, 8080);
    LIBSSH2_LISTENER *l2 = libssh2_channel_forward_listen(session, 9090);

    (void)libssh2_channel_forward_cancel(l1);
    (void)libssh2_channel_forward_cancel(l2);

    teardown_session(session);
}

int main() {
    variant_forward_listen_accept();
    variant_forward_listen_ex();
    variant_direct_tcpip();
    variant_direct_tcpip_ex();
    variant_multiple_listeners();
    return 0;
}
