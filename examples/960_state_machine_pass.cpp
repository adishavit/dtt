// 960_state_machine_pass.cpp
// Tests: Protocol state machine - correct transition sequence
// From BLOG.md: Connection state machine (closed → open → authenticated)

struct Connection {
    // State: 0=closed, 1=open, 2=authenticated

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

    [[clang::annotate("dtt:requires=_state >= 1")]]
    [[clang::annotate("dtt:ensures=_state == 0")]]
    void disconnect() {}
};

// Variant 1: Correct full sequence
void test_correct_sequence() {
    Connection c;          // _state = 0
    c.connect();           // _state = 1
    c.authenticate();      // _state = 2
    c.send("hello");       // OK: _state == 2
}

// Variant 2: Connect and disconnect without auth
void test_connect_disconnect() {
    Connection c;          // _state = 0
    c.connect();           // _state = 1
    c.disconnect();        // OK: _state >= 1
}

// Variant 3: Full cycle then reconnect
void test_reconnect() {
    Connection c;
    c.connect();
    c.authenticate();
    c.send("msg1");
    c.disconnect();        // _state = 0
    c.connect();           // OK: can reconnect
    c.authenticate();
    c.send("msg2");        // OK
}

int main() {
    test_correct_sequence();
    test_connect_disconnect();
    test_reconnect();
    return 0;
}
