// 977_crypto_key_fail.cpp
// Tests: Cryptographic state - decrypt with wrong key caught
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

void test_wrong_key() {
    SecureData secret;
    Key k1(42);
    Key k2(99);            // Different key!
    secret.encrypt(k1);    // @key_id = 42
    secret.decrypt(k2);    // ERROR: @key_id(42) != k2._key_id(99)
}

int main() {
    test_wrong_key();
    return 0;
}
