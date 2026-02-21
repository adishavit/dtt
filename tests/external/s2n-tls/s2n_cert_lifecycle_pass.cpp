// s2n cert chain lifecycle — correct usage patterns (should COMPILE)
//
// Tests that the s2n_cert_chain_and_key_new -> modify -> free lifecycle is
// tracked correctly by DTT ghost field @cert_alive. Each variant allocates
// a cert chain, uses it while alive, and frees it before scope exit.

#include <s2n.h>

// Variant 1: cert_chain_and_key_new -> load_pem -> cert_chain_and_key_free
void variant_load_pem() {
    struct s2n_cert_chain_and_key *cert = s2n_cert_chain_and_key_new();  // @cert_alive = 1
    (void)s2n_cert_chain_and_key_load_pem(cert, "cert.pem", "key.pem"); // requires @cert_alive != 0
    (void)s2n_cert_chain_and_key_free(cert);                             // ensures @cert_alive = 0
}

// Variant 2: cert_chain_and_key_new -> set_ctx -> cert_chain_and_key_free
void variant_set_ctx() {
    struct s2n_cert_chain_and_key *cert = s2n_cert_chain_and_key_new();  // @cert_alive = 1
    int user_data = 42;
    (void)s2n_cert_chain_and_key_set_ctx(cert, &user_data);             // requires @cert_alive != 0
    (void)s2n_cert_chain_and_key_free(cert);                             // ensures @cert_alive = 0
}

int main() {
    variant_load_pem();
    variant_set_ctx();
    return 0;
}
