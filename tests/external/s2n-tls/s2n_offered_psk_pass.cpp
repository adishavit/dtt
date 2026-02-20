// s2n offered PSK list operations -- correct usage patterns (should COMPILE)
//
// Tests that s2n offered PSK list functions (has_next, next, reread,
// get_identity, choose_psk) are tracked correctly by DTT. These functions
// are typically called within a PSK selection callback on the server side.

#include <s2n.h>

// Variant 1: iterate offered PSK list and choose one
// In practice, psk_list is provided by s2n via the PSK selection callback.
// Here we use a pointer to exercise the function signatures for DTT coverage.
void variant_iterate_offered_psk(struct s2n_offered_psk_list *psk_list) {
    struct s2n_offered_psk *offered = s2n_offered_psk_new();

    while (s2n_offered_psk_list_has_next(psk_list)) {
        (void)s2n_offered_psk_list_next(psk_list, offered);

        uint8_t *identity = nullptr;
        uint16_t identity_size = 0;
        (void)s2n_offered_psk_get_identity(offered, &identity, &identity_size);
    }

    // Rewind and choose the first PSK
    (void)s2n_offered_psk_list_reread(psk_list);
    if (s2n_offered_psk_list_has_next(psk_list)) {
        (void)s2n_offered_psk_list_next(psk_list, offered);
        (void)s2n_offered_psk_list_choose_psk(psk_list, offered);
    }

    (void)s2n_offered_psk_free(&offered);
}

// Variant 2: PSK selection callback pattern with full server config
void variant_psk_selection_server() {
    struct s2n_config *config = s2n_config_new();                  // @config_alive = 1
    (void)s2n_config_set_psk_mode(config, S2N_PSK_MODE_EXTERNAL);
    (void)s2n_config_set_psk_selection_callback(config, nullptr, nullptr);

    struct s2n_connection *conn = s2n_connection_new(S2N_SERVER);  // @conn_state = 1
    (void)s2n_connection_set_config(conn, config);

    (void)s2n_connection_free(conn);                               // @conn_state = 0
    (void)s2n_config_free(config);                                 // @config_alive = 0
}

int main() {
    variant_psk_selection_server();
    return 0;
}
