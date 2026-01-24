// 976_crypto_key_pass.cpp
// Tests: Cryptographic state - encrypt/decrypt with matching key IDs
// From BLOG.md: Cryptographic State example

struct Key {
    [[clang::annotate("dtt:ensures=_key_id == arg0")]]
    Key(int id) {}
};

struct SecureData {
    [[clang::annotate("dtt:ensures=@encrypted == 0")]]
    SecureData() {}

    [[clang::annotate("dtt:requires=@encrypted == 0")]]
    [[clang::annotate("dtt:ensures=@encrypted == 1")]]
    [[clang::annotate("dtt:ensures=@key_id == arg0._key_id")]]
    void encrypt(Key& key) {}

    [[clang::annotate("dtt:requires=@encrypted == 1")]]
    [[clang::annotate("dtt:requires=@key_id == arg0._key_id")]]
    [[clang::annotate("dtt:ensures=@encrypted == 0")]]
    void decrypt(Key& key) {}
};

[[clang::annotate("dtt:requires=arg0.@encrypted == 1")]]
void send_secure(SecureData& data) {}

// Variant 1: Encrypt then send
void test_encrypt_send() {
    SecureData secret;
    Key k(42);
    secret.encrypt(k);     // @encrypted=1, @key_id=42
    send_secure(secret);   // OK: @encrypted == 1
}

// Variant 2: Encrypt then decrypt with same key
void test_encrypt_decrypt_same_key() {
    SecureData secret;
    Key k(99);
    secret.encrypt(k);     // @encrypted=1, @key_id=99
    secret.decrypt(k);     // OK: @key_id matches
}

// Variant 3: Full cycle - encrypt, send, decrypt
void test_full_cycle() {
    SecureData secret;
    Key k(7);
    secret.encrypt(k);
    send_secure(secret);
    secret.decrypt(k);     // OK
}

int main() {
    test_encrypt_send();
    test_encrypt_decrypt_same_key();
    test_full_cycle();
    return 0;
}
