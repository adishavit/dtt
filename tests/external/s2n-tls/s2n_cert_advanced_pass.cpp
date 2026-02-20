// s2n certificate chain advanced operations -- correct usage patterns (should COMPILE)
//
// Tests all s2n_cert_chain_and_key_* functions are tracked correctly by DTT
// ghost field @cert_alive. Each variant allocates a cert chain, calls various
// cert APIs while alive, and frees it before scope exit.

#include <s2n.h>

// Variant 1: cert_new -> load_pem_bytes -> cert_free
void variant_load_pem_bytes() {
    struct s2n_cert_chain_and_key *cert = s2n_cert_chain_and_key_new();  // @cert_alive = 1

    uint8_t chain_pem[] = "-----BEGIN CERTIFICATE-----\nfake\n-----END CERTIFICATE-----";
    uint8_t key_pem[] = "-----BEGIN PRIVATE KEY-----\nfake\n-----END PRIVATE KEY-----";
    (void)s2n_cert_chain_and_key_load_pem_bytes(cert, chain_pem, sizeof(chain_pem),
                                                 key_pem, sizeof(key_pem));

    (void)s2n_cert_chain_and_key_free(cert);                             // @cert_alive = 0
}

// Variant 2: cert_new -> load_public_pem_bytes -> cert_free
void variant_load_public_pem_bytes() {
    struct s2n_cert_chain_and_key *cert = s2n_cert_chain_and_key_new();  // @cert_alive = 1

    uint8_t chain_pem[] = "-----BEGIN CERTIFICATE-----\nfake\n-----END CERTIFICATE-----";
    (void)s2n_cert_chain_and_key_load_public_pem_bytes(cert, chain_pem, sizeof(chain_pem));

    (void)s2n_cert_chain_and_key_free(cert);                             // @cert_alive = 0
}

// Variant 3: cert_new -> set_ctx -> get_ctx -> cert_free
void variant_cert_ctx() {
    struct s2n_cert_chain_and_key *cert = s2n_cert_chain_and_key_new();  // @cert_alive = 1

    int user_data = 42;
    (void)s2n_cert_chain_and_key_set_ctx(cert, &user_data);             // requires @cert_alive != 0
    (void)s2n_cert_chain_and_key_get_ctx(cert);                          // requires @cert_alive != 0

    (void)s2n_cert_chain_and_key_free(cert);                             // @cert_alive = 0
}

// Variant 4: full cert lifecycle -- load PEM, add to config, use in connection
void variant_full_cert_lifecycle() {
    struct s2n_cert_chain_and_key *cert = s2n_cert_chain_and_key_new();  // @cert_alive = 1
    (void)s2n_cert_chain_and_key_load_pem(cert, "server.pem", "server-key.pem");

    struct s2n_config *config = s2n_config_new();                        // @config_alive = 1
    (void)s2n_config_add_cert_chain_and_key_to_store(config, cert);

    struct s2n_connection *conn = s2n_connection_new(S2N_SERVER);        // @conn_state = 1
    (void)s2n_connection_set_config(conn, config);
    (void)s2n_connection_free(conn);                                     // @conn_state = 0
    (void)s2n_config_free(config);                                       // @config_alive = 0
    (void)s2n_cert_chain_and_key_free(cert);                             // @cert_alive = 0
}

// Variant 5: multiple certs added to a single config
void variant_multi_cert_config() {
    struct s2n_cert_chain_and_key *rsa_cert = s2n_cert_chain_and_key_new();   // @cert_alive = 1
    struct s2n_cert_chain_and_key *ecdsa_cert = s2n_cert_chain_and_key_new(); // @cert_alive = 1

    (void)s2n_cert_chain_and_key_load_pem(rsa_cert, "rsa.pem", "rsa-key.pem");
    (void)s2n_cert_chain_and_key_load_pem(ecdsa_cert, "ecdsa.pem", "ecdsa-key.pem");

    struct s2n_config *config = s2n_config_new();                        // @config_alive = 1
    (void)s2n_config_add_cert_chain_and_key_to_store(config, rsa_cert);
    (void)s2n_config_add_cert_chain_and_key_to_store(config, ecdsa_cert);

    (void)s2n_config_free(config);                                       // @config_alive = 0
    (void)s2n_cert_chain_and_key_free(rsa_cert);                         // @cert_alive = 0
    (void)s2n_cert_chain_and_key_free(ecdsa_cert);                       // @cert_alive = 0
}

int main() {
    variant_load_pem_bytes();
    variant_load_public_pem_bytes();
    variant_cert_ctx();
    variant_full_cert_lifecycle();
    variant_multi_cert_config();
    return 0;
}
