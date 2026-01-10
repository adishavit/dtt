// Example 498: Ghost state key binding - correct key should pass

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
    s.encrypt(42);     // @encrypted = 1, @key_id = 42
    s.decrypt(42);     // OK: arg0(42) == @key_id(42)
}
