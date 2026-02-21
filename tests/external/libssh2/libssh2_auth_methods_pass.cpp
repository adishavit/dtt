// libssh2 authentication methods -- correct usage patterns (should COMPILE)
//
// Tests that all authentication methods satisfy DTT ghost field constraints
// when called after a successful handshake (@session_state == 2).
// Covers: userauth_password, userauth_publickey_fromfile,
// userauth_publickey_frommemory, userauth_keyboard_interactive,
// userauth_authenticated, userauth_list.
// Also covers _ex variants via macro expansion.

#include <libssh2.h>
#include <string.h>

// Keyboard-interactive callback (required by the API)
static void kbd_callback(const char *name, int name_len,
                         const char *instruction, int instruction_len,
                         int num_prompts,
                         const LIBSSH2_USERAUTH_KBDINT_PROMPT *prompts,
                         LIBSSH2_USERAUTH_KBDINT_RESPONSE *responses,
                         void **abstract) {
    (void)name; (void)name_len;
    (void)instruction; (void)instruction_len;
    (void)num_prompts; (void)prompts;
    (void)responses; (void)abstract;
}

// Variant 1: password authentication (macro -> userauth_password_ex)
//   init -> handshake -> userauth_password -> disconnect -> free
void variant_password() {
    LIBSSH2_SESSION *session = libssh2_session_init();
    (void)libssh2_session_handshake(session, 0);

    (void)libssh2_userauth_password(session, "alice", "secret123");

    (void)libssh2_session_disconnect(session, "done");
    libssh2_session_free(session);
}

// Variant 2: public key from file (macro -> userauth_publickey_fromfile_ex)
//   init -> handshake -> userauth_publickey_fromfile -> disconnect -> free
void variant_pubkey_fromfile() {
    LIBSSH2_SESSION *session = libssh2_session_init();
    (void)libssh2_session_handshake(session, 0);

    (void)libssh2_userauth_publickey_fromfile(
        session, "bob",
        "/home/bob/.ssh/id_ed25519.pub",
        "/home/bob/.ssh/id_ed25519",
        "passphrase");

    (void)libssh2_session_disconnect(session, "done");
    libssh2_session_free(session);
}

// Variant 3: public key from memory
//   init -> handshake -> userauth_publickey_frommemory -> disconnect -> free
void variant_pubkey_frommemory() {
    LIBSSH2_SESSION *session = libssh2_session_init();
    (void)libssh2_session_handshake(session, 0);

    const char *pubkey_data = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAA";
    const char *privkey_data = "-----BEGIN OPENSSH PRIVATE KEY-----\n...";

    (void)libssh2_userauth_publickey_frommemory(
        session, "charlie", strlen("charlie"),
        pubkey_data, strlen(pubkey_data),
        privkey_data, strlen(privkey_data),
        "passphrase");

    (void)libssh2_session_disconnect(session, "done");
    libssh2_session_free(session);
}

// Variant 4: keyboard-interactive authentication
//   init -> handshake -> userauth_keyboard_interactive -> disconnect -> free
void variant_keyboard_interactive() {
    LIBSSH2_SESSION *session = libssh2_session_init();
    (void)libssh2_session_handshake(session, 0);

    (void)libssh2_userauth_keyboard_interactive(
        session, "dave", kbd_callback);

    (void)libssh2_session_disconnect(session, "done");
    libssh2_session_free(session);
}

// Variant 5: userauth_list to enumerate available methods
//   init -> handshake -> userauth_list -> password auth -> disconnect -> free
void variant_userauth_list() {
    LIBSSH2_SESSION *session = libssh2_session_init();
    (void)libssh2_session_handshake(session, 0);

    char *methods = libssh2_userauth_list(session, "eve",
                                          (unsigned int)strlen("eve"));
    (void)methods;

    (void)libssh2_userauth_password(session, "eve", "password");

    (void)libssh2_session_disconnect(session, "done");
    libssh2_session_free(session);
}

// Variant 6: userauth_authenticated query after authentication
//   init -> handshake -> password -> userauth_authenticated -> disconnect -> free
void variant_userauth_authenticated() {
    LIBSSH2_SESSION *session = libssh2_session_init();
    (void)libssh2_session_handshake(session, 0);

    (void)libssh2_userauth_password(session, "frank", "pw");

    int authenticated = libssh2_userauth_authenticated(session);
    (void)authenticated;

    (void)libssh2_session_disconnect(session, "done");
    libssh2_session_free(session);
}

// Variant 7: password_ex direct call (skipping macro)
//   init -> handshake -> userauth_password_ex -> disconnect -> free
void variant_password_ex() {
    LIBSSH2_SESSION *session = libssh2_session_init();
    (void)libssh2_session_handshake(session, 0);

    (void)libssh2_userauth_password_ex(
        session, "grace", 5, "secret", 6, NULL);

    (void)libssh2_session_disconnect(session, "done");
    libssh2_session_free(session);
}

// Variant 8: publickey_fromfile_ex direct call (skipping macro)
//   init -> handshake -> userauth_publickey_fromfile_ex -> disconnect -> free
void variant_pubkey_fromfile_ex() {
    LIBSSH2_SESSION *session = libssh2_session_init();
    (void)libssh2_session_handshake(session, 0);

    (void)libssh2_userauth_publickey_fromfile_ex(
        session, "hank", 4,
        "/home/hank/.ssh/id_rsa.pub",
        "/home/hank/.ssh/id_rsa",
        "passphrase");

    (void)libssh2_session_disconnect(session, "done");
    libssh2_session_free(session);
}

int main() {
    variant_password();
    variant_pubkey_fromfile();
    variant_pubkey_frommemory();
    variant_keyboard_interactive();
    variant_userauth_list();
    variant_userauth_authenticated();
    variant_password_ex();
    variant_pubkey_fromfile_ex();
    return 0;
}
