// Example 497: Ghost state multi - send without authenticate should fail

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
};

int main() {
    Channel ch;
    ch.connect();      // @state = 1
    // NOT calling authenticate()
    ch.send("hello");  // ERROR: @state is 1, not 2; @authenticated is 0  dtt-error {{@state == 2}} dtt-error {{@authenticated == 1}}
}
