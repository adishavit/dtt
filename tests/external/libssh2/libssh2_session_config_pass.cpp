// libssh2 session configuration -- correct usage patterns (should COMPILE)
//
// Tests that all session configuration operations satisfy DTT ghost field
// constraints. Configuration functions require valid session pointers;
// some (like method_pref) require specific states.
// Covers: set_blocking, set_timeout, last_error, last_errno, hostkey,
// method_pref, keepalive_config, keepalive_send.

#include <libssh2.h>

// Variant 1: session configuration before handshake
//   init -> set_blocking -> set_timeout -> method_pref -> keepalive_config ->
//   handshake -> auth -> disconnect -> free
void variant_config_before_handshake() {
    LIBSSH2_SESSION *session = libssh2_session_init();

    libssh2_session_set_blocking(session, 1);
    libssh2_session_set_timeout(session, 30000);
    (void)libssh2_session_method_pref(session, LIBSSH2_METHOD_KEX,
                                       "diffie-hellman-group14-sha256");
    libssh2_keepalive_config(session, 1, 60);

    (void)libssh2_session_handshake(session, 0);
    (void)libssh2_userauth_password(session, "user", "pw");
    (void)libssh2_session_disconnect(session, "done");
    libssh2_session_free(session);
}

// Variant 2: error queries after handshake
//   init -> handshake -> last_error -> last_errno -> auth -> disconnect -> free
void variant_error_queries() {
    LIBSSH2_SESSION *session = libssh2_session_init();
    (void)libssh2_session_handshake(session, 0);

    char *errmsg = NULL;
    int errmsg_len = 0;
    (void)libssh2_session_last_error(session, &errmsg, &errmsg_len, 0);
    (void)libssh2_session_last_errno(session);

    (void)libssh2_userauth_password(session, "user", "pw");
    (void)libssh2_session_disconnect(session, "done");
    libssh2_session_free(session);
}

// Variant 3: hostkey retrieval after handshake
//   init -> handshake -> hostkey -> auth -> disconnect -> free
void variant_hostkey_after_handshake() {
    LIBSSH2_SESSION *session = libssh2_session_init();
    (void)libssh2_session_handshake(session, 0);

    size_t len;
    int type;
    const char *key = libssh2_session_hostkey(session, &len, &type);
    (void)key;

    (void)libssh2_userauth_password(session, "user", "pw");
    (void)libssh2_session_disconnect(session, "done");
    libssh2_session_free(session);
}

// Variant 4: keepalive send during authenticated session
//   init -> handshake -> auth -> keepalive_config -> keepalive_send -> disconnect -> free
void variant_keepalive_send() {
    LIBSSH2_SESSION *session = libssh2_session_init();
    (void)libssh2_session_handshake(session, 0);
    (void)libssh2_userauth_password(session, "user", "pw");

    libssh2_keepalive_config(session, 1, 30);
    int seconds_to_next = 0;
    (void)libssh2_keepalive_send(session, &seconds_to_next);

    (void)libssh2_session_disconnect(session, "done");
    libssh2_session_free(session);
}

// Variant 5: non-blocking mode with timeout and error checking
//   init -> set_blocking(0) -> set_timeout -> handshake -> last_errno ->
//   auth -> disconnect -> free
void variant_nonblocking_with_errors() {
    LIBSSH2_SESSION *session = libssh2_session_init();
    libssh2_session_set_blocking(session, 0);
    libssh2_session_set_timeout(session, 10000);

    (void)libssh2_session_handshake(session, 0);
    (void)libssh2_session_last_errno(session);

    (void)libssh2_userauth_password(session, "user", "pw");

    char *errmsg = NULL;
    int errmsg_len = 0;
    (void)libssh2_session_last_error(session, &errmsg, &errmsg_len, 0);

    (void)libssh2_session_disconnect(session, "done");
    libssh2_session_free(session);
}

// Variant 6: multiple method_pref calls for different algorithms
//   init -> method_pref(KEX) -> method_pref(HOSTKEY) -> method_pref(CRYPT_CS) ->
//   handshake -> auth -> disconnect -> free
void variant_multiple_method_prefs() {
    LIBSSH2_SESSION *session = libssh2_session_init();

    (void)libssh2_session_method_pref(session, LIBSSH2_METHOD_KEX,
                                       "diffie-hellman-group14-sha256");
    (void)libssh2_session_method_pref(session, LIBSSH2_METHOD_HOSTKEY,
                                       "ssh-ed25519");
    (void)libssh2_session_method_pref(session, LIBSSH2_METHOD_CRYPT_CS,
                                       "aes256-ctr");

    (void)libssh2_session_handshake(session, 0);
    (void)libssh2_userauth_password(session, "user", "pw");
    (void)libssh2_session_disconnect(session, "done");
    libssh2_session_free(session);
}

int main() {
    variant_config_before_handshake();
    variant_error_queries();
    variant_hostkey_after_handshake();
    variant_keepalive_send();
    variant_nonblocking_with_errors();
    variant_multiple_method_prefs();
    return 0;
}
