// libssh2 known hosts operations -- correct usage patterns (should COMPILE)
//
// Tests that all known host operations satisfy DTT ghost field constraints.
// Known host operations require a valid session pointer.
// Covers: knownhost_init, knownhost_free, knownhost_readfile,
// knownhost_check, knownhost_checkp.

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

// Variant 1: init and free known hosts collection
//   knownhost_init -> knownhost_free
void variant_init_free() {
    LIBSSH2_SESSION *session = make_authed_session();

    LIBSSH2_KNOWNHOSTS *kh = libssh2_knownhost_init(session);
    libssh2_knownhost_free(kh);

    teardown_session(session);
}

// Variant 2: load known hosts from file and check a host
//   knownhost_init -> knownhost_readfile -> knownhost_check -> knownhost_free
void variant_readfile_check() {
    LIBSSH2_SESSION *session = make_authed_session();

    LIBSSH2_KNOWNHOSTS *kh = libssh2_knownhost_init(session);
    (void)libssh2_knownhost_readfile(kh, "/home/user/.ssh/known_hosts",
                                     LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    const char *key = "AAAAB3NzaC1yc2EAAAA";
    (void)libssh2_knownhost_check(kh, "example.com", key, 20,
                                  LIBSSH2_KNOWNHOST_TYPE_PLAIN |
                                  LIBSSH2_KNOWNHOST_KEYENC_RAW,
                                  NULL);
    libssh2_knownhost_free(kh);

    teardown_session(session);
}

// Variant 3: checkp (with port) after loading known hosts
//   knownhost_init -> knownhost_readfile -> knownhost_checkp -> knownhost_free
void variant_readfile_checkp() {
    LIBSSH2_SESSION *session = make_authed_session();

    LIBSSH2_KNOWNHOSTS *kh = libssh2_knownhost_init(session);
    (void)libssh2_knownhost_readfile(kh, "/home/user/.ssh/known_hosts",
                                     LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    const char *key = "AAAAB3NzaC1yc2EAAAA";
    (void)libssh2_knownhost_checkp(kh, "example.com", 22, key, 20,
                                   LIBSSH2_KNOWNHOST_TYPE_PLAIN |
                                   LIBSSH2_KNOWNHOST_KEYENC_RAW,
                                   NULL);
    libssh2_knownhost_free(kh);

    teardown_session(session);
}

// Variant 4: known hosts used during handshake verification workflow
//   session_init -> handshake -> knownhost_init -> readfile -> checkp ->
//   knownhost_free -> auth -> disconnect -> free
void variant_host_verification_workflow() {
    LIBSSH2_SESSION *session = libssh2_session_init();
    (void)libssh2_session_handshake(session, 0);

    LIBSSH2_KNOWNHOSTS *kh = libssh2_knownhost_init(session);
    (void)libssh2_knownhost_readfile(kh, "/home/user/.ssh/known_hosts",
                                     LIBSSH2_KNOWNHOST_FILE_OPENSSH);

    size_t len;
    int type;
    const char *fingerprint = libssh2_session_hostkey(session, &len, &type);
    (void)fingerprint;

    const char *key = "AAAAB3NzaC1yc2EAAAA";
    (void)libssh2_knownhost_checkp(kh, "server.example.com", 22,
                                   key, 20,
                                   LIBSSH2_KNOWNHOST_TYPE_PLAIN |
                                   LIBSSH2_KNOWNHOST_KEYENC_RAW,
                                   NULL);
    libssh2_knownhost_free(kh);

    (void)libssh2_userauth_password(session, "user", "pw");
    (void)libssh2_session_disconnect(session, "done");
    libssh2_session_free(session);
}

int main() {
    variant_init_free();
    variant_readfile_check();
    variant_readfile_checkp();
    variant_host_verification_workflow();
    return 0;
}
