// Example 494: Ghost state test: Should produce DTT error - @encrypted is 0, not 1

struct SecureData {
    [[clang::annotate("dtt:ensures=@encrypted == 1")]]
    void encrypt() {}

    [[clang::annotate("dtt:requires=@encrypted == 1")]]
    void send() {}
};

int main() {
    SecureData s;
    // NOT calling s.encrypt() - so @encrypted is still 0 (default)
    s.send();     // ERROR: @encrypted is 0, requires 1
}
