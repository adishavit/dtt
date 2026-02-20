// libssh2 global init/exit and session_init_ex -- correct usage (should COMPILE)
//
// Tests that global initialization (libssh2_init/libssh2_exit) and the
// explicit session_init_ex variant work correctly with DTT.
// Also covers session_startup (deprecated handshake) and session_disconnect_ex.

#include <libssh2.h>

// Variant 1: global init and exit wrapping a full session lifecycle
//   libssh2_init -> session_init -> handshake -> auth -> disconnect -> free -> libssh2_exit
void variant_global_init_exit() {
    (void)libssh2_init(0);

    LIBSSH2_SESSION *session = libssh2_session_init();
    (void)libssh2_session_handshake(session, 0);
    (void)libssh2_userauth_password(session, "user", "pw");
    (void)libssh2_session_disconnect(session, "done");
    libssh2_session_free(session);

    libssh2_exit();
}

// Variant 2: session_init_ex with explicit allocators (all NULL = default)
//   session_init_ex -> handshake -> auth -> disconnect -> free
void variant_session_init_ex() {
    LIBSSH2_SESSION *session = libssh2_session_init_ex(NULL, NULL, NULL, NULL);
    (void)libssh2_session_handshake(session, 0);
    (void)libssh2_userauth_password(session, "user", "pw");
    (void)libssh2_session_disconnect(session, "done");
    libssh2_session_free(session);
}

// Variant 3: session_disconnect_ex with explicit reason code
//   session_init -> handshake -> auth -> disconnect_ex -> free
void variant_disconnect_ex() {
    LIBSSH2_SESSION *session = libssh2_session_init();
    (void)libssh2_session_handshake(session, 0);
    (void)libssh2_userauth_password(session, "user", "pw");

    (void)libssh2_session_disconnect_ex(session,
        SSH_DISCONNECT_BY_APPLICATION, "shutting down", "");

    libssh2_session_free(session);
}

// Variant 4: global init with LIBSSH2_INIT_NO_CRYPTO flag
//   libssh2_init(NO_CRYPTO) -> session lifecycle -> libssh2_exit
void variant_init_no_crypto() {
    (void)libssh2_init(LIBSSH2_INIT_NO_CRYPTO);

    LIBSSH2_SESSION *session = libssh2_session_init();
    (void)libssh2_session_handshake(session, 0);
    (void)libssh2_userauth_password(session, "user", "pw");
    (void)libssh2_session_disconnect(session, "done");
    libssh2_session_free(session);

    libssh2_exit();
}

// Variant 5: session_startup (deprecated handshake function, same semantics)
//   session_init -> session_startup -> auth -> disconnect -> free
void variant_session_startup() {
    LIBSSH2_SESSION *session = libssh2_session_init();
    (void)libssh2_session_startup(session, 0);
    (void)libssh2_userauth_password(session, "user", "pw");
    (void)libssh2_session_disconnect(session, "done");
    libssh2_session_free(session);
}

int main() {
    variant_global_init_exit();
    variant_session_init_ex();
    variant_disconnect_ex();
    variant_init_no_crypto();
    variant_session_startup();
    return 0;
}
