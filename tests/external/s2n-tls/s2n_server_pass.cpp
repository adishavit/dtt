// s2n server-side operations -- correct usage patterns (should COMPILE)
//
// Tests server-specific s2n operations including full server setup,
// config binding to multiple connections, and server-side protocol
// negotiation. All resources are properly freed before scope exit.

#include <s2n.h>

// Variant 1: full server setup -- init, config, cert, connection, negotiate, send/recv, shutdown
void variant_full_server_setup() {
    (void)s2n_init();

    struct s2n_config *config = s2n_config_new();                        // @config_alive = 1
    (void)s2n_config_set_cipher_preferences(config, "default_tls13");
    const char *protocols[] = { "h2", "http/1.1" };
    (void)s2n_config_set_protocol_preferences(config, protocols, 2);

    struct s2n_cert_chain_and_key *cert = s2n_cert_chain_and_key_new();  // @cert_alive = 1
    (void)s2n_cert_chain_and_key_load_pem(cert, "server.pem", "server-key.pem");
    (void)s2n_config_add_cert_chain_and_key_to_store(config, cert);

    struct s2n_connection *conn = s2n_connection_new(S2N_SERVER);        // @conn_state = 1
    s2n_blocked_status blocked;
    char buf[1024];

    (void)s2n_connection_set_config(conn, config);
    (void)s2n_connection_set_fd(conn, 0);                                // @conn_state = 2
    (void)s2n_negotiate(conn, &blocked);                                 // @conn_state = 3
    (void)s2n_recv(conn, buf, sizeof(buf), &blocked);
    (void)s2n_send(conn, "response", 8, &blocked);
    (void)s2n_shutdown(conn, &blocked);                                  // @conn_state = 4
    (void)s2n_connection_free(conn);                                     // @conn_state = 0

    (void)s2n_cert_chain_and_key_free(cert);                             // @cert_alive = 0
    (void)s2n_config_free(config);                                       // @config_alive = 0
    (void)s2n_cleanup();
}

// Variant 2: config bound to multiple connections
void variant_config_multi_conn() {
    struct s2n_config *config = s2n_config_new();                        // @config_alive = 1
    (void)s2n_config_set_cipher_preferences(config, "default_tls13");

    struct s2n_connection *conn1 = s2n_connection_new(S2N_SERVER);       // @conn_state = 1
    struct s2n_connection *conn2 = s2n_connection_new(S2N_SERVER);       // @conn_state = 1

    (void)s2n_connection_set_config(conn1, config);
    (void)s2n_connection_set_config(conn2, config);

    (void)s2n_connection_free(conn1);                                    // @conn_state = 0
    (void)s2n_connection_free(conn2);                                    // @conn_state = 0
    (void)s2n_config_free(config);                                       // @config_alive = 0
}

// Variant 3: server with client auth
void variant_server_client_auth() {
    struct s2n_config *config = s2n_config_new();                        // @config_alive = 1
    (void)s2n_config_set_cipher_preferences(config, "default_tls13");
    (void)s2n_config_set_verification_ca_location(config, "/etc/ssl/certs/ca-certificates.crt", nullptr);

    struct s2n_connection *conn = s2n_connection_new(S2N_SERVER);        // @conn_state = 1
    (void)s2n_connection_set_config(conn, config);
    (void)s2n_connection_set_client_auth_type(conn, S2N_CERT_AUTH_REQUIRED);

    s2n_cert_auth_type auth_type;
    (void)s2n_connection_get_client_auth_type(conn, &auth_type);

    (void)s2n_connection_free(conn);                                     // @conn_state = 0
    (void)s2n_config_free(config);                                       // @config_alive = 0
}

// Variant 4: server protocol preference append
void variant_server_protocol_append() {
    struct s2n_config *config = s2n_config_new();                        // @config_alive = 1
    (void)s2n_config_append_protocol_preference(config, (const uint8_t *)"h2", 2);
    (void)s2n_config_append_protocol_preference(config, (const uint8_t *)"http/1.1", 8);

    struct s2n_connection *conn = s2n_connection_new(S2N_SERVER);        // @conn_state = 1
    s2n_blocked_status blocked;

    (void)s2n_connection_set_config(conn, config);
    (void)s2n_connection_set_fd(conn, 0);                                // @conn_state = 2
    (void)s2n_negotiate(conn, &blocked);                                 // @conn_state = 3

    (void)s2n_get_application_protocol(conn);

    (void)s2n_shutdown(conn, &blocked);                                  // @conn_state = 4
    (void)s2n_connection_free(conn);                                     // @conn_state = 0
    (void)s2n_config_free(config);                                       // @config_alive = 0
}

// Variant 5: process init/cleanup with cleanup_final
void variant_init_cleanup_final() {
    (void)s2n_init();
    (void)s2n_cleanup_final();
}

int main() {
    variant_full_server_setup();
    variant_config_multi_conn();
    variant_server_client_auth();
    variant_server_protocol_append();
    variant_init_cleanup_final();
    return 0;
}
