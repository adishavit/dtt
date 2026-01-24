// 970_information_flow_pass.cpp
// Tests: Information flow - proper declassification before logging
// From BLOG.md: Information Flow example

template<typename T>
struct Labeled {
    // Secrecy: 0=public, 1=internal, 2=secret, 3=top_secret
    // Integrity: 0=untrusted, 1=trusted, 2=verified

    [[clang::annotate("dtt:ensures=_secrecy == arg1")]]
    [[clang::annotate("dtt:ensures=_integrity == arg2")]]
    Labeled(T data, int secrecy, int integrity) {}

    [[clang::annotate("dtt:requires=arg0._secrecy >= _secrecy")]]
    void copy_to(Labeled& dest) {}

    [[clang::annotate("dtt:requires=_integrity >= 2")]]
    [[clang::annotate("dtt:ensures=_secrecy == arg0")]]
    void declassify(int new_level) {}
};

[[clang::annotate("dtt:requires=arg0._secrecy == 0")]]
void log_public(Labeled<int>& data) {}

[[clang::annotate("dtt:requires=arg0._secrecy <= 1")]]
[[clang::annotate("dtt:requires=arg0._integrity >= 1")]]
void send_external(Labeled<int>& data) {}

// Variant 1: Public data can be logged
void test_log_public() {
    Labeled<int> pub(42, 0, 0);  // _secrecy = 0 (public)
    log_public(pub);              // OK: _secrecy == 0
}

// Variant 2: Declassify then log
void test_declassify_then_log() {
    Labeled<int> secret(42, 3, 2);  // _secrecy=3, _integrity=2
    secret.declassify(0);            // _secrecy = 0 (requires integrity >= 2)
    log_public(secret);              // OK: now public
}

// Variant 3: Copy to higher secrecy level
void test_copy_up() {
    Labeled<int> pub(1, 0, 0);       // _secrecy = 0
    Labeled<int> secret(2, 3, 0);    // _secrecy = 3
    pub.copy_to(secret);             // OK: 3 >= 0
}

// Variant 4: Internal data to external with proper integrity
void test_send_internal() {
    Labeled<int> internal(42, 1, 1);  // _secrecy=1, _integrity=1
    send_external(internal);          // OK: secrecy <= 1, integrity >= 1
}

int main() {
    test_log_public();
    test_declassify_then_log();
    test_copy_up();
    test_send_internal();
    return 0;
}
