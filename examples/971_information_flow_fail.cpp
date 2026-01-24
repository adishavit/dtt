// 971_information_flow_fail.cpp
// Tests: Information flow - secret data cannot leak to public
// From BLOG.md: Information Flow example

template<typename T>
struct Labeled {
    [[clang::annotate("dtt:ensures=_secrecy == arg1")]]
    Labeled(T data, int secrecy) {}

    [[clang::annotate("dtt:requires=arg0._secrecy >= _secrecy")]]
    void copy_to(Labeled& dest) {}
};

[[clang::annotate("dtt:requires=arg0._secrecy == 0")]]
void log_public(Labeled<int>& data) {}

void test_leak_secret() {
    Labeled<int> secret(42, 3);   // _secrecy = 3 (top_secret)
    log_public(secret);            // ERROR: _secrecy(3) != 0
}

int main() {
    test_leak_secret();
    return 0;
}
