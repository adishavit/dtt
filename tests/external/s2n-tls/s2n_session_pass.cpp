// s2n session resumption operations -- correct usage patterns (should COMPILE)
//
// Tests that s2n session-related functions (get_session, set_session,
// get_session_length, get_session_id, is_session_resumed) are called on
// valid connections with proper DTT ghost field states.

#include <s2n.h>

// Variant 1: session serialization after negotiate
void variant_get_session() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1
    s2n_blocked_status blocked;

    (void)s2n_connection_set_fd(conn, 0);                          // @conn_state = 2
    (void)s2n_negotiate(conn, &blocked);                           // @conn_state = 3

    int session_len = s2n_connection_get_session_length(conn);
    uint8_t session_buf[4096];
    (void)s2n_connection_get_session(conn, session_buf, sizeof(session_buf));

    (void)s2n_shutdown(conn, &blocked);                            // @conn_state = 4
    (void)s2n_connection_free(conn);                               // @conn_state = 0
}

// Variant 2: session restoration on a new connection
void variant_set_session() {
    // First connection: negotiate and save session
    struct s2n_connection *conn1 = s2n_connection_new(S2N_CLIENT); // @conn_state = 1
    s2n_blocked_status blocked;

    (void)s2n_connection_set_fd(conn1, 0);                         // @conn_state = 2
    (void)s2n_negotiate(conn1, &blocked);                          // @conn_state = 3

    uint8_t session_buf[4096];
    int session_len = s2n_connection_get_session_length(conn1);
    (void)s2n_connection_get_session(conn1, session_buf, sizeof(session_buf));

    (void)s2n_shutdown(conn1, &blocked);                           // @conn_state = 4
    (void)s2n_connection_free(conn1);                              // @conn_state = 0

    // Second connection: restore session
    struct s2n_connection *conn2 = s2n_connection_new(S2N_CLIENT); // @conn_state = 1
    (void)s2n_connection_set_session(conn2, session_buf, session_len);

    (void)s2n_connection_free(conn2);                              // @conn_state = 0
}

// Variant 3: session ID queries
void variant_session_id() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1
    s2n_blocked_status blocked;

    (void)s2n_connection_set_fd(conn, 0);                          // @conn_state = 2
    (void)s2n_negotiate(conn, &blocked);                           // @conn_state = 3

    int id_len = s2n_connection_get_session_id_length(conn);
    uint8_t session_id[256];
    (void)s2n_connection_get_session_id(conn, session_id, sizeof(session_id));
    (void)s2n_connection_is_session_resumed(conn);

    (void)s2n_shutdown(conn, &blocked);                            // @conn_state = 4
    (void)s2n_connection_free(conn);                               // @conn_state = 0
}

// Variant 4: session with config-level ticket support
void variant_session_tickets() {
    struct s2n_config *config = s2n_config_new();                  // @config_alive = 1
    (void)s2n_config_set_session_tickets_onoff(config, 1);
    s2n_blocked_status blocked;

    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1
    (void)s2n_connection_set_config(conn, config);
    (void)s2n_connection_set_fd(conn, 0);                          // @conn_state = 2
    (void)s2n_negotiate(conn, &blocked);                           // @conn_state = 3

    int session_len = s2n_connection_get_session_length(conn);
    uint8_t session_buf[4096];
    (void)s2n_connection_get_session(conn, session_buf, sizeof(session_buf));
    (void)s2n_connection_is_session_resumed(conn);

    (void)s2n_shutdown(conn, &blocked);                            // @conn_state = 4
    (void)s2n_connection_free(conn);                               // @conn_state = 0
    (void)s2n_config_free(config);                                 // @config_alive = 0
}

int main() {
    variant_get_session();
    variant_set_session();
    variant_session_id();
    variant_session_tickets();
    return 0;
}
