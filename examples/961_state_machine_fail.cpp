// 961_state_machine_fail.cpp
// Tests: Protocol state machine - incorrect transition (send before authenticate)
// From BLOG.md: Connection state machine

struct Connection {
    [[clang::annotate("dtt:ensures=_state == 0")]]
    Connection() {}

    [[clang::annotate("dtt:requires=_state == 0")]]
    [[clang::annotate("dtt:ensures=_state == 1")]]
    void connect() {}

    [[clang::annotate("dtt:requires=_state == 1")]]
    [[clang::annotate("dtt:ensures=_state == 2")]]
    void authenticate() {}

    [[clang::annotate("dtt:requires=_state == 2")]]
    void send(const char* data) {}
};

void test_send_before_auth() {
    Connection c;          // _state = 0
    c.connect();           // _state = 1
    c.send("hello");       // ERROR: _state is 1, requires 2  dtt-error {{_state == 2}}
}

int main() {
    test_send_before_auth();
    return 0;
}
