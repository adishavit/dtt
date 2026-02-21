// s2n connection lifecycle — correct usage patterns (should COMPILE)
//
// Tests that the s2n_connection_new -> configure -> negotiate -> transfer ->
// shutdown -> free lifecycle is tracked correctly by DTT ghost field
// @conn_state. Each variant follows a valid state progression through
// the connection state machine.

#include <s2n.h>

// Variant 1: Full client lifecycle with config binding
//   new(CLIENT) -> set_config -> set_fd -> negotiate -> send -> shutdown -> free
void variant_client_full() {
    struct s2n_config *config = s2n_config_new();          // @config_alive = 1
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1

    s2n_blocked_status blocked;

    (void)s2n_connection_set_config(conn, config);         // requires @conn_state == 1
    (void)s2n_connection_set_fd(conn, 0);                  // requires @conn_state == 1, ensures @conn_state = 2
    (void)s2n_negotiate(conn, &blocked);                   // ensures @conn_state = 3
    (void)s2n_send(conn, "hello", 5, &blocked);            // requires @conn_state == 3
    (void)s2n_shutdown(conn, &blocked);                    // requires @conn_state == 3, ensures @conn_state = 4
    (void)s2n_connection_free(conn);                       // ensures @conn_state = 0
    (void)s2n_config_free(config);                         // ensures @config_alive = 0
}

// Variant 2: Server lifecycle without explicit config binding
//   new(SERVER) -> set_fd -> negotiate -> recv -> shutdown -> free
void variant_server_no_config() {
    struct s2n_connection *conn = s2n_connection_new(S2N_SERVER);  // @conn_state = 1

    s2n_blocked_status blocked;
    char buf[1024];

    (void)s2n_connection_set_fd(conn, 0);                  // requires @conn_state == 1, ensures @conn_state = 2
    (void)s2n_negotiate(conn, &blocked);                   // ensures @conn_state = 3
    (void)s2n_recv(conn, buf, sizeof(buf), &blocked);      // requires @conn_state == 3
    (void)s2n_shutdown(conn, &blocked);                    // requires @conn_state == 3, ensures @conn_state = 4
    (void)s2n_connection_free(conn);                       // ensures @conn_state = 0
}

int main() {
    variant_client_full();
    variant_server_no_config();
    return 0;
}
