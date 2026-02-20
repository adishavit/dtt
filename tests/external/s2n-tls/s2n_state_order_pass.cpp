// s2n full state sequence — correct end-to-end ordering (should COMPILE)
//
// Tests a complete s2n usage scenario: init, config setup with certificate,
// connection lifecycle through negotiation, bidirectional data transfer,
// and teardown. Validates that all ghost fields (@config_alive, @cert_alive,
// @conn_state) are satisfied at every API call boundary.

#include <s2n.h>

// Variant 1: Full end-to-end sequence with all resource types
//   config_new -> set_cipher -> cert_new -> load_pem -> add_cert_to_store ->
//   connection_new -> set_config -> set_fd -> negotiate ->
//   send -> recv -> shutdown -> connection_free ->
//   cert_free -> config_free
void variant_full_sequence() {
    struct s2n_config *config = s2n_config_new();                        // @config_alive = 1
    (void)s2n_config_set_cipher_preferences(config, "default_tls13");   // requires @config_alive != 0

    struct s2n_cert_chain_and_key *cert = s2n_cert_chain_and_key_new(); // @cert_alive = 1
    (void)s2n_cert_chain_and_key_load_pem(cert, "cert.pem", "key.pem");// requires @cert_alive != 0
    (void)s2n_config_add_cert_chain_and_key_to_store(config, cert);    // requires @config_alive != 0

    struct s2n_connection *conn = s2n_connection_new(S2N_SERVER);       // @conn_state = 1
    s2n_blocked_status blocked;
    char buf[1024];

    (void)s2n_connection_set_config(conn, config);                      // requires @conn_state == 1, @config_alive != 0
    (void)s2n_connection_set_fd(conn, 0);                               // requires @conn_state == 1, ensures @conn_state = 2
    (void)s2n_negotiate(conn, &blocked);                                // ensures @conn_state = 3
    (void)s2n_send(conn, "response", 8, &blocked);                     // requires @conn_state == 3
    (void)s2n_recv(conn, buf, sizeof(buf), &blocked);                  // requires @conn_state == 3
    (void)s2n_shutdown(conn, &blocked);                                 // requires @conn_state == 3, ensures @conn_state = 4
    (void)s2n_connection_free(conn);                                    // ensures @conn_state = 0

    (void)s2n_cert_chain_and_key_free(cert);                            // ensures @cert_alive = 0
    (void)s2n_config_free(config);                                      // ensures @config_alive = 0
}

int main() {
    variant_full_sequence();
    return 0;
}
