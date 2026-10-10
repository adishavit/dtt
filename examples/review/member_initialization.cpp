// Selected independently so a later error cannot conceal a missed earlier one.
#include <utility>
struct Connection {
    int _state = 0;
    [[clang::annotate("dtt:requires=_state == 0")]]
    [[clang::annotate("dtt:ensures=_state == 1")]] void connect();
    [[clang::annotate("dtt:requires=_state == 1")]]
    [[clang::annotate("dtt:ensures=_state == 2")]] void authenticate();
    [[clang::annotate("dtt:requires=_state == 2")]] void send();
};
struct Override {
    int _state = 0;
    constexpr Override() : _state(2) {}
    [[clang::annotate("dtt:requires=_state == 2")]] void send();
    [[clang::annotate("dtt:requires=_state == 0")]] void connect();
};
struct BodyOverride {
    int _state = 0;
    constexpr BodyOverride() { _state = 2; }
    [[clang::annotate("dtt:requires=_state == 2")]] void send();
    [[clang::annotate("dtt:requires=_state == 0")]] void connect();
};
struct Sized {
    int len = 2;
    [[clang::annotate("dtt:requires=len == 2")]] void check();
};
int unknown();
struct RuntimeOverride {
    int _state = 0;
    RuntimeOverride() { _state = unknown(); }
    [[clang::annotate("dtt:requires=_state == 2")]] void send();
};
void test() {
#if CASE == 0
    Connection c; c.send();
#elif CASE == 1
    Connection c; c.authenticate();
#elif CASE == 2
    Connection c{}; c.send();
#elif CASE == 3
    Connection c; c.connect(); c.authenticate(); c.send();
#elif CASE == 4
    Connection c{2}; c.send();
#elif CASE == 5
    Override c; c.send();
#elif CASE == 6
    BodyOverride c; c.send();
#elif CASE == 7
    RuntimeOverride c; c.send(); // No proven state after runtime construction.
#elif CASE == 8
    Connection a; a.connect(); a.authenticate(); Connection c(a); c.send();
#elif CASE == 9
    Connection a; a.connect(); a.authenticate(); Connection c(std::move(a)); c.send();
#elif CASE == 10
    Connection c{._state = 2}; c.send();
#elif CASE == 11
    Connection a; Connection c(a); c.send();
#elif CASE == 12
    Override c; c.connect();
#elif CASE == 13
    BodyOverride c; c.connect();
#elif CASE == 14
    Connection c{2}; c.connect();
#elif CASE == 15
    Sized c{}; c.check();
#elif CASE == 16
    Sized c{2}; c.check();
#endif
}
