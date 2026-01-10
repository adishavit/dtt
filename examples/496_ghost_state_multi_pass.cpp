// Example 496: Ghost state multi - state machine

struct Channel {
    [[clang::annotate("dtt:ensures=@state == 1")]]
    void connect() {}

    [[clang::annotate("dtt:requires=@state == 1"),
      clang::annotate("dtt:ensures=@state == 2"),
      clang::annotate("dtt:ensures=@authenticated == 1")]]
    void authenticate() {}

    [[clang::annotate("dtt:requires=@state == 2"),
      clang::annotate("dtt:requires=@authenticated == 1")]]
    void send(const char* data) {}

    [[clang::annotate("dtt:requires=@state >= 1"),
      clang::annotate("dtt:ensures=@state == 0")]]
    void disconnect() {}
};

int main() {
    Channel ch;
    ch.connect();        // @state = 1
    ch.authenticate();   // @state = 2, @authenticated = 1
    ch.send("hello");    // OK: @state == 2, @authenticated == 1
    ch.disconnect();     // @state = 0
}
