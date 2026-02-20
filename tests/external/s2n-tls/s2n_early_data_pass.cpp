// s2n early data (0-RTT) operations -- correct usage patterns (should COMPILE)
//
// Tests that s2n early data functions are tracked correctly by DTT.
// Early data (0-RTT) allows clients to send data before the TLS handshake
// completes, improving latency for repeated connections.

#include <s2n.h>

// Variant 1: send early data on a client connection
void variant_send_early_data() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1
    s2n_blocked_status blocked;

    uint8_t data[] = "early hello";
    ssize_t data_sent = 0;
    (void)s2n_send_early_data(conn, data, sizeof(data), &data_sent, &blocked);

    (void)s2n_connection_free(conn);                               // @conn_state = 0
}

// Variant 2: recv early data on a server connection
void variant_recv_early_data() {
    struct s2n_connection *conn = s2n_connection_new(S2N_SERVER);  // @conn_state = 1
    s2n_blocked_status blocked;

    uint8_t buf[1024];
    ssize_t data_recv = 0;
    (void)s2n_recv_early_data(conn, buf, sizeof(buf), &data_recv, &blocked);

    (void)s2n_connection_free(conn);                               // @conn_state = 0
}

// Variant 3: early data status and size queries
void variant_early_data_queries() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1

    s2n_early_data_status_t status;
    (void)s2n_connection_get_early_data_status(conn, &status);

    uint32_t remaining = 0;
    (void)s2n_connection_get_remaining_early_data_size(conn, &remaining);

    uint32_t max_size = 0;
    (void)s2n_connection_get_max_early_data_size(conn, &max_size);

    (void)s2n_connection_free(conn);                               // @conn_state = 0
}

// Variant 4: server-side early data configuration
void variant_server_early_data_config() {
    struct s2n_config *config = s2n_config_new();                  // @config_alive = 1
    (void)s2n_config_set_server_max_early_data_size(config, 16384);
    (void)s2n_config_set_early_data_cb(config, nullptr);

    struct s2n_connection *conn = s2n_connection_new(S2N_SERVER);  // @conn_state = 1
    (void)s2n_connection_set_config(conn, config);

    (void)s2n_connection_set_server_max_early_data_size(conn, 8192);
    uint8_t ctx_data[] = "early_ctx";
    (void)s2n_connection_set_server_early_data_context(conn, ctx_data, sizeof(ctx_data));

    (void)s2n_connection_free(conn);                               // @conn_state = 0
    (void)s2n_config_free(config);                                 // @config_alive = 0
}

// Variant 5: full early data workflow -- config, send, negotiate, transfer
void variant_full_early_data_workflow() {
    struct s2n_config *config = s2n_config_new();                  // @config_alive = 1
    (void)s2n_config_set_server_max_early_data_size(config, 16384);
    (void)s2n_config_set_session_tickets_onoff(config, 1);

    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1
    s2n_blocked_status blocked;

    (void)s2n_connection_set_config(conn, config);

    // Send early data before handshake
    uint8_t early[] = "early request";
    ssize_t sent = 0;
    (void)s2n_send_early_data(conn, early, sizeof(early), &sent, &blocked);

    // Query early data status
    s2n_early_data_status_t status;
    (void)s2n_connection_get_early_data_status(conn, &status);
    uint32_t remaining = 0;
    (void)s2n_connection_get_remaining_early_data_size(conn, &remaining);

    (void)s2n_connection_free(conn);                               // @conn_state = 0
    (void)s2n_config_free(config);                                 // @config_alive = 0
}

int main() {
    variant_send_early_data();
    variant_recv_early_data();
    variant_early_data_queries();
    variant_server_early_data_config();
    variant_full_early_data_workflow();
    return 0;
}
