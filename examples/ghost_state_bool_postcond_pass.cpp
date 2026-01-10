// Test ghost state with ensures_if_true/ensures_if_false
// This tests whether @-prefixed properties work with boolean postconditions

struct SecureData {
    [[clang::annotate("dtt:ensures=@encrypted == 1")]]
    void encrypt() {}

    [[clang::annotate("dtt:ensures_if_true=@encrypted == 1"),
      clang::annotate("dtt:ensures_if_false=@encrypted == 0")]]
    bool isEncrypted() const { return true; }

    [[clang::annotate("dtt:requires=@encrypted == 1")]]
    void send() {}
};

int main() {
    SecureData s;
    if (s.isEncrypted()) {
        s.send();  // Should be OK: @encrypted == 1 proven by guard
    }
}
