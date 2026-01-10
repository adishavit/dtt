// Example 499: Ghost state key binding - wrong key should fail

struct SecureData {
    [[clang::annotate("dtt:ensures=@encrypted == 1"),
      clang::annotate("dtt:ensures=@key_id == arg0")]]
    void encrypt(int key_id) {}

    [[clang::annotate("dtt:requires=@encrypted == 1"),
      clang::annotate("dtt:requires=arg0 == @key_id")]]  // arg on LEFT
    void decrypt(int key_id) {}
};

int main() {
    SecureData s;
    s.encrypt(1);      // @encrypted = 1, @key_id = 1
    s.decrypt(2);      // ERROR: arg0(2) != @key_id(1)
}
