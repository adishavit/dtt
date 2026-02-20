// s2n connection advanced operations -- correct usage patterns (should COMPILE)
//
// Tests that all s2n_connection_* set/get/query functions are tracked
// correctly by DTT ghost fields. Each variant creates a connection, calls
// the relevant APIs in a valid state, and frees all resources before scope exit.

#include <s2n.h>

// Variant 1: set_read_fd and set_write_fd (separate FD setting)
void variant_separate_fds() {
    struct s2n_config *config = s2n_config_new();
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1

    (void)s2n_connection_set_config(conn, config);
    (void)s2n_connection_set_read_fd(conn, 3);                     // requires @conn_state == 1
    (void)s2n_connection_set_write_fd(conn, 4);                    // requires @conn_state == 1

    (void)s2n_connection_free(conn);                               // @conn_state = 0
    (void)s2n_config_free(config);
}

// Variant 2: custom I/O callbacks (recv_cb, send_cb, recv_ctx, send_ctx)
void variant_custom_io() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1
    int io_ctx = 0;

    (void)s2n_connection_set_recv_cb(conn, nullptr);
    (void)s2n_connection_set_send_cb(conn, nullptr);
    (void)s2n_connection_set_recv_ctx(conn, &io_ctx);
    (void)s2n_connection_set_send_ctx(conn, &io_ctx);

    (void)s2n_connection_free(conn);                               // @conn_state = 0
}

// Variant 3: server name and blinding
void variant_server_name_and_blinding() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1

    (void)s2n_set_server_name(conn, "example.com");
    (void)s2n_connection_set_blinding(conn, S2N_SELF_SERVICE_BLINDING);

    (void)s2n_connection_free(conn);                               // @conn_state = 0
}

// Variant 4: connection-level cipher and protocol preferences
void variant_conn_cipher_proto_prefs() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1

    (void)s2n_connection_set_cipher_preferences(conn, "default_tls13");
    (void)s2n_connection_append_protocol_preference(conn, (const uint8_t *)"h2", 2);
    const char *protocols[] = { "h2", "http/1.1" };
    (void)s2n_connection_set_protocol_preferences(conn, protocols, 2);

    (void)s2n_connection_free(conn);                               // @conn_state = 0
}

// Variant 5: connection context (set_ctx / get_ctx)
void variant_conn_ctx() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1
    int user_data = 99;

    (void)s2n_connection_set_ctx(conn, &user_data);
    (void)s2n_connection_get_ctx(conn);

    (void)s2n_connection_free(conn);                               // @conn_state = 0
}

// Variant 6: connection-level client auth type
void variant_client_auth() {
    struct s2n_connection *conn = s2n_connection_new(S2N_SERVER);  // @conn_state = 1
    s2n_cert_auth_type auth_type;

    (void)s2n_connection_set_client_auth_type(conn, S2N_CERT_AUTH_REQUIRED);
    (void)s2n_connection_get_client_auth_type(conn, &auth_type);

    (void)s2n_connection_free(conn);                               // @conn_state = 0
}

// Variant 7: connection queries after negotiate (protocol, cipher, alert, etc.)
void variant_conn_queries_after_negotiate() {
    struct s2n_config *config = s2n_config_new();
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1
    s2n_blocked_status blocked;

    (void)s2n_connection_set_config(conn, config);
    (void)s2n_connection_set_fd(conn, 0);                          // @conn_state = 2
    (void)s2n_negotiate(conn, &blocked);                           // @conn_state = 3

    // Query operations on a negotiated connection
    (void)s2n_connection_get_cipher(conn);
    (void)s2n_connection_get_actual_protocol_version(conn);
    (void)s2n_connection_get_client_protocol_version(conn);
    (void)s2n_connection_get_server_protocol_version(conn);
    (void)s2n_connection_get_client_hello_version(conn);
    (void)s2n_connection_get_alert(conn);
    (void)s2n_connection_get_delay(conn);
    (void)s2n_connection_get_selected_cert(conn);
    (void)s2n_connection_get_handshake_type_name(conn);
    (void)s2n_connection_get_last_message_name(conn);
    (void)s2n_get_server_name(conn);
    (void)s2n_get_application_protocol(conn);
    (void)s2n_connection_is_ocsp_stapled(conn);

    (void)s2n_shutdown(conn, &blocked);                            // @conn_state = 4
    (void)s2n_connection_free(conn);                               // @conn_state = 0
    (void)s2n_config_free(config);
}

// Variant 8: peer cert chain query
void variant_peer_cert() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1
    s2n_blocked_status blocked;

    (void)s2n_connection_set_fd(conn, 0);                          // @conn_state = 2
    (void)s2n_negotiate(conn, &blocked);                           // @conn_state = 3

    struct s2n_cert_chain_and_key *peer_chain = s2n_cert_chain_and_key_new();
    (void)s2n_connection_get_peer_cert_chain(conn, peer_chain);

    (void)s2n_shutdown(conn, &blocked);                            // @conn_state = 4
    (void)s2n_connection_free(conn);                               // @conn_state = 0
    (void)s2n_cert_chain_and_key_free(peer_chain);
}

// Variant 9: sendv and sendv_with_offset
void variant_sendv() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1
    s2n_blocked_status blocked;

    (void)s2n_connection_set_fd(conn, 0);                          // @conn_state = 2
    (void)s2n_negotiate(conn, &blocked);                           // @conn_state = 3

    struct iovec bufs[2];
    char buf1[] = "hello";
    char buf2[] = "world";
    bufs[0].iov_base = buf1;
    bufs[0].iov_len = 5;
    bufs[1].iov_base = buf2;
    bufs[1].iov_len = 5;

    (void)s2n_sendv(conn, bufs, 2, &blocked);                     // requires @conn_state == 3
    (void)s2n_sendv_with_offset(conn, bufs, 2, 0, &blocked);      // requires @conn_state == 3

    (void)s2n_shutdown(conn, &blocked);                            // @conn_state = 4
    (void)s2n_connection_free(conn);                               // @conn_state = 0
}

// Variant 10: peek and peek_buffered
void variant_peek() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1
    s2n_blocked_status blocked;

    (void)s2n_connection_set_fd(conn, 0);                          // @conn_state = 2
    (void)s2n_negotiate(conn, &blocked);                           // @conn_state = 3

    (void)s2n_peek(conn);                                          // requires @conn_state == 3
    (void)s2n_peek_buffered(conn);                                 // requires @conn_state == 3

    (void)s2n_shutdown(conn, &blocked);                            // @conn_state = 4
    (void)s2n_connection_free(conn);                               // @conn_state = 0
}

// Variant 11: shutdown_send (half-close)
void variant_shutdown_send() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1
    s2n_blocked_status blocked;

    (void)s2n_connection_set_fd(conn, 0);                          // @conn_state = 2
    (void)s2n_negotiate(conn, &blocked);                           // @conn_state = 3

    (void)s2n_shutdown_send(conn, &blocked);                       // requires @conn_state == 3

    (void)s2n_shutdown(conn, &blocked);                            // requires @conn_state == 3, ensures @conn_state = 4
    (void)s2n_connection_free(conn);                               // @conn_state = 0
}

// Variant 12: free_handshake and release_buffers (memory optimization after negotiate)
void variant_free_handshake() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1
    s2n_blocked_status blocked;

    (void)s2n_connection_set_fd(conn, 0);                          // @conn_state = 2
    (void)s2n_negotiate(conn, &blocked);                           // @conn_state = 3

    (void)s2n_connection_free_handshake(conn);                     // requires @conn_state == 3
    (void)s2n_connection_release_buffers(conn);

    (void)s2n_shutdown(conn, &blocked);                            // @conn_state = 4
    (void)s2n_connection_free(conn);                               // @conn_state = 0
}

// Variant 13: connection wipe and reuse
void variant_wipe_reuse() {
    struct s2n_config *config = s2n_config_new();
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1
    s2n_blocked_status blocked;

    (void)s2n_connection_set_config(conn, config);
    (void)s2n_connection_set_fd(conn, 0);                          // @conn_state = 2
    (void)s2n_negotiate(conn, &blocked);                           // @conn_state = 3
    (void)s2n_shutdown(conn, &blocked);                            // @conn_state = 4

    (void)s2n_connection_wipe(conn);                               // @conn_state = 5

    (void)s2n_connection_free(conn);                               // @conn_state = 0
    (void)s2n_config_free(config);
}

// Variant 14: negotiated PSK identity query
void variant_psk_identity_query() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1

    uint16_t identity_len = 0;
    (void)s2n_connection_get_negotiated_psk_identity_length(conn, &identity_len);
    uint8_t identity[256];
    (void)s2n_connection_get_negotiated_psk_identity(conn, identity, sizeof(identity));

    (void)s2n_connection_free(conn);                               // @conn_state = 0
}

// Variant 15: full state walk: 0->1->2->3->4->5->0
void variant_full_state_walk() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1 (created)
    s2n_blocked_status blocked;

    (void)s2n_connection_set_fd(conn, 0);                          // @conn_state = 2 (configured)
    (void)s2n_negotiate(conn, &blocked);                           // @conn_state = 3 (negotiated)
    (void)s2n_shutdown(conn, &blocked);                            // @conn_state = 4 (shutdown)
    (void)s2n_connection_wipe(conn);                               // @conn_state = 5 (wiped)
    (void)s2n_connection_free(conn);                               // @conn_state = 0 (freed)
}

int main() {
    variant_separate_fds();
    variant_custom_io();
    variant_server_name_and_blinding();
    variant_conn_cipher_proto_prefs();
    variant_conn_ctx();
    variant_client_auth();
    variant_conn_queries_after_negotiate();
    variant_peer_cert();
    variant_sendv();
    variant_peek();
    variant_shutdown_send();
    variant_free_handshake();
    variant_wipe_reuse();
    variant_psk_identity_query();
    variant_full_state_walk();
    return 0;
}
