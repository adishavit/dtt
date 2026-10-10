// Example 494: Ghost state test: Should produce DTT error - @encrypted is 0, not 1

struct SecureData {
    [[clang::annotate("dtt:ensures=@encrypted == 0")]]
    SecureData() {}

    [[clang::annotate("dtt:ensures=@encrypted == 1")]]
    void encrypt() {}

    [[clang::annotate("dtt:requires=@encrypted == 1")]]
    void send() {}
};

int main() {
    SecureData s;
    // The constructor establishes 0; encrypt() would establish 1.
    s.send();     // ERROR: @encrypted is 0, requires 1
}
