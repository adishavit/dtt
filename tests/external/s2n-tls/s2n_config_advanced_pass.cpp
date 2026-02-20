// s2n config advanced operations -- correct usage patterns (should COMPILE)
//
// Tests that all s2n_config_* modification functions are tracked correctly
// by DTT ghost field @config_alive. Each variant allocates a config,
// calls one or more config modification APIs while the config is alive,
// then frees it before scope exit.

#include <s2n.h>

// Variant 1: config_new_minimal -> set_cipher_preferences -> config_free
void variant_minimal_config() {
    struct s2n_config *config = s2n_config_new_minimal();     // @config_alive = 1
    (void)s2n_config_set_cipher_preferences(config, "default_tls13");
    (void)s2n_config_free(config);                            // @config_alive = 0
}

// Variant 2: protocol preferences (list and append)
void variant_protocol_preferences() {
    struct s2n_config *config = s2n_config_new();             // @config_alive = 1
    const char *protocols[] = { "h2", "http/1.1" };
    (void)s2n_config_set_protocol_preferences(config, protocols, 2);
    (void)s2n_config_append_protocol_preference(config, (const uint8_t *)"h2", 2);
    (void)s2n_config_free(config);                            // @config_alive = 0
}

// Variant 3: session ticket configuration
void variant_session_tickets() {
    struct s2n_config *config = s2n_config_new();             // @config_alive = 1
    (void)s2n_config_set_session_tickets_onoff(config, 1);
    (void)s2n_config_set_ticket_encrypt_decrypt_key_lifetime(config, 3600);
    (void)s2n_config_set_ticket_decrypt_key_lifetime(config, 7200);
    uint8_t ticket_key_name[] = "2024key1";
    uint8_t ticket_key[32] = {0};
    (void)s2n_config_add_ticket_crypto_key(config, ticket_key_name, 8, ticket_key, 32, 0);
    (void)s2n_config_free(config);                            // @config_alive = 0
}

// Variant 4: session cache
void variant_session_cache() {
    struct s2n_config *config = s2n_config_new();             // @config_alive = 1
    (void)s2n_config_set_session_cache_onoff(config, 1);
    (void)s2n_config_free(config);                            // @config_alive = 0
}

// Variant 5: client hello callback
void variant_client_hello_cb() {
    struct s2n_config *config = s2n_config_new();             // @config_alive = 1
    (void)s2n_config_set_client_hello_cb(config, nullptr, nullptr);
    (void)s2n_config_free(config);                            // @config_alive = 0
}

// Variant 6: verify after sign
void variant_verify_after_sign() {
    struct s2n_config *config = s2n_config_new();             // @config_alive = 1
    (void)s2n_config_set_verify_after_sign(config, S2N_VERIFY_AFTER_SIGN_ENABLED);
    (void)s2n_config_free(config);                            // @config_alive = 0
}

// Variant 7: OCSP stapling check
void variant_ocsp_check() {
    struct s2n_config *config = s2n_config_new();             // @config_alive = 1
    (void)s2n_config_set_check_stapled_ocsp_response(config, 1);
    (void)s2n_config_free(config);                            // @config_alive = 0
}

// Variant 8: disable X.509 verification
void variant_disable_x509() {
    struct s2n_config *config = s2n_config_new();             // @config_alive = 1
    (void)s2n_config_disable_x509_verification(config);
    (void)s2n_config_free(config);                            // @config_alive = 0
}

// Variant 9: max cert chain depth
void variant_max_cert_depth() {
    struct s2n_config *config = s2n_config_new();             // @config_alive = 1
    (void)s2n_config_set_max_cert_chain_depth(config, 10);
    (void)s2n_config_free(config);                            // @config_alive = 0
}

// Variant 10: send/recv buffer configuration
void variant_buffer_config() {
    struct s2n_config *config = s2n_config_new();             // @config_alive = 1
    (void)s2n_config_set_send_buffer_size(config, 4096);
    (void)s2n_config_set_recv_multi_record(config, true);
    (void)s2n_config_free(config);                            // @config_alive = 0
}

// Variant 11: config context (set_ctx / get_ctx)
void variant_config_ctx() {
    struct s2n_config *config = s2n_config_new();             // @config_alive = 1
    int user_data = 42;
    (void)s2n_config_set_ctx(config, &user_data);
    void *ctx = nullptr;
    (void)s2n_config_get_ctx(config, &ctx);
    (void)s2n_config_free(config);                            // @config_alive = 0
}

// Variant 12: CA location and trust store
void variant_trust_store() {
    struct s2n_config *config = s2n_config_new();             // @config_alive = 1
    (void)s2n_config_set_verification_ca_location(config, "/etc/ssl/certs/ca-certificates.crt", nullptr);
    (void)s2n_config_add_pem_to_trust_store(config, "-----BEGIN CERTIFICATE-----\nfake\n-----END CERTIFICATE-----");
    (void)s2n_config_free(config);                            // @config_alive = 0
}

// Variant 13: cert tiebreak callback
void variant_cert_tiebreak() {
    struct s2n_config *config = s2n_config_new();             // @config_alive = 1
    (void)s2n_config_set_cert_tiebreak_callback(config, nullptr);
    (void)s2n_config_free(config);                            // @config_alive = 0
}

// Variant 14: clock configuration (monotonic and wall)
void variant_clock_config() {
    struct s2n_config *config = s2n_config_new();             // @config_alive = 1
    (void)s2n_config_set_monotonic_clock(config, nullptr, nullptr);
    (void)s2n_config_set_wall_clock(config, nullptr, nullptr);
    (void)s2n_config_free(config);                            // @config_alive = 0
}

// Variant 15: alert behavior
void variant_alert_behavior() {
    struct s2n_config *config = s2n_config_new();             // @config_alive = 1
    (void)s2n_config_set_alert_behavior(config, S2N_ALERT_FAIL_ON_WARNINGS);
    (void)s2n_config_free(config);                            // @config_alive = 0
}

// Variant 16: extension data
void variant_extension_data() {
    struct s2n_config *config = s2n_config_new();             // @config_alive = 1
    uint8_t data[] = { 0x01, 0x02 };
    (void)s2n_config_set_extension_data(config, S2N_EXTENSION_CERTIFICATE_TRANSPARENCY, data, sizeof(data));
    (void)s2n_config_free(config);                            // @config_alive = 0
}

// Variant 17: PSK mode and selection callback on config
void variant_psk_config() {
    struct s2n_config *config = s2n_config_new();             // @config_alive = 1
    (void)s2n_config_set_psk_mode(config, S2N_PSK_MODE_EXTERNAL);
    (void)s2n_config_set_psk_selection_callback(config, nullptr, nullptr);
    (void)s2n_config_free(config);                            // @config_alive = 0
}

// Variant 18: early data callback on config
void variant_early_data_cb() {
    struct s2n_config *config = s2n_config_new();             // @config_alive = 1
    (void)s2n_config_set_early_data_cb(config, nullptr);
    (void)s2n_config_set_server_max_early_data_size(config, 16384);
    (void)s2n_config_free(config);                            // @config_alive = 0
}

// Variant 19: full config setup -- many options combined
void variant_full_config_setup() {
    struct s2n_config *config = s2n_config_new();             // @config_alive = 1

    (void)s2n_config_set_cipher_preferences(config, "default_tls13");
    const char *protocols[] = { "h2", "http/1.1" };
    (void)s2n_config_set_protocol_preferences(config, protocols, 2);
    (void)s2n_config_set_session_tickets_onoff(config, 1);
    (void)s2n_config_set_verify_after_sign(config, S2N_VERIFY_AFTER_SIGN_ENABLED);
    (void)s2n_config_set_check_stapled_ocsp_response(config, 1);
    (void)s2n_config_set_max_cert_chain_depth(config, 5);
    (void)s2n_config_set_send_buffer_size(config, 8192);
    (void)s2n_config_set_recv_multi_record(config, true);
    (void)s2n_config_disable_x509_verification(config);
    (void)s2n_config_set_alert_behavior(config, S2N_ALERT_FAIL_ON_WARNINGS);

    struct s2n_cert_chain_and_key *cert = s2n_cert_chain_and_key_new();
    (void)s2n_cert_chain_and_key_load_pem(cert, "cert.pem", "key.pem");
    (void)s2n_config_add_cert_chain_and_key_to_store(config, cert);

    (void)s2n_cert_chain_and_key_free(cert);                  // @cert_alive = 0
    (void)s2n_config_free(config);                            // @config_alive = 0
}

int main() {
    variant_minimal_config();
    variant_protocol_preferences();
    variant_session_tickets();
    variant_session_cache();
    variant_client_hello_cb();
    variant_verify_after_sign();
    variant_ocsp_check();
    variant_disable_x509();
    variant_max_cert_depth();
    variant_buffer_config();
    variant_config_ctx();
    variant_trust_store();
    variant_cert_tiebreak();
    variant_clock_config();
    variant_alert_behavior();
    variant_extension_data();
    variant_psk_config();
    variant_early_data_cb();
    variant_full_config_setup();
    return 0;
}
