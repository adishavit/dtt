// Ghost state test: @encrypted exists only in compiler tracking, not as runtime field
// The @ prefix is invalid in C++ identifiers, so this property cannot be declared or tampered with

struct SecureData {
    // No @encrypted field here — it's ghost state!

    [[clang::annotate("dtt:ensures=@encrypted == 1")]]
    void encrypt() {}

    [[clang::annotate("dtt:requires=@encrypted == 1")]]
    void send() {}
};

int main() {
    SecureData s;
    s.encrypt();  // Sets @encrypted = 1
    s.send();     // OK: @encrypted == 1
}
