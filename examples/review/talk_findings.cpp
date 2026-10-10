// Reproductions reported by the parallel talk session. CASE selects the finding.
#if CASE == 5
#include <vector>
#include <utility>
void consume(std::vector<int>);
void list_then_move() {
    std::vector<int> v = {1, 2, 3};
    v[0];
    consume(std::move(v));
    v[0];
}
#elif CASE == 6
#include <vector>
void unknown_parameter(std::vector<int>& v) { v.front(); v.back(); }
#elif CASE == 7
struct Vec {
    int len = 0;
    [[clang::annotate("dtt:requires=arg0 >= 20")]] void needsTwenty(int);
};
int getValue();
void unguarded() {
    Vec a, b, c;
    a.len = getValue(); b.len = a.len + 5; c.len = b.len + 10;
    c.needsTwenty(c.len);
}
void guarded() {
    Vec a, b, c;
    a.len = getValue(); b.len = a.len + 5; c.len = b.len + 10;
    if (a.len >= 5) c.needsTwenty(c.len);
}
#elif CASE == 8
struct Connection {
    int _state = 0;
    [[clang::annotate("dtt:requires=_state == 0")]]
    [[clang::annotate("dtt:ensures=_state == 1")]] void connect();
    [[clang::annotate("dtt:requires=_state == 1")]]
    [[clang::annotate("dtt:ensures=_state == 2")]] void authenticate();
    [[clang::annotate("dtt:requires=_state == 2")]] void send(const char*);
};
void out_of_order() { Connection c; c.send("x"); c.authenticate(); c.connect(); }
#elif CASE == 9
struct Secret {
    [[clang::annotate("dtt:requires=@encrypted == 1")]] void send();
    [[clang::annotate("dtt:ensures=@encrypted == 1")]] void encrypt();
};
void fresh() { Secret secret; secret.send(); }
void initialized() { Secret secret; secret.encrypt(); secret.send(); }
#endif
