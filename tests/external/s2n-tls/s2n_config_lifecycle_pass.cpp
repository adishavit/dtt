// s2n config lifecycle — correct usage patterns (should COMPILE)
//
// Tests that the s2n_config_new -> modify -> free lifecycle is tracked
// correctly by DTT ghost field @config_alive. Each variant allocates a
// config, modifies it while alive, and frees it before scope exit.

#include <s2n.h>

// Variant 1: config_new -> set_cipher_preferences -> config_free
void variant_cipher_prefs() {
    struct s2n_config *config = s2n_config_new();          // @config_alive = 1
    (void)s2n_config_set_cipher_preferences(config, "default_tls13");  // requires @config_alive != 0
    (void)s2n_config_free(config);                         // ensures @config_alive = 0
}

// Variant 2: config_new -> add cert to store -> config_free
void variant_cert_store() {
    struct s2n_cert_chain_and_key *cert = s2n_cert_chain_and_key_new();  // @cert_alive = 1
    struct s2n_config *config = s2n_config_new();                        // @config_alive = 1

    (void)s2n_config_add_cert_chain_and_key_to_store(config, cert);     // requires @config_alive != 0
    (void)s2n_config_free(config);                                       // ensures @config_alive = 0
    (void)s2n_cert_chain_and_key_free(cert);                             // ensures @cert_alive = 0
}

int main() {
    variant_cipher_prefs();
    variant_cert_store();
    return 0;
}
