// Each case checks facts after a call whose contract is (or is not) established.
struct State {
    int _state = 0;
    [[clang::annotate("dtt:requires=arg0 >= 1")]]
    [[clang::annotate("dtt:ensures=_state = 2")]] void advance(int);
    [[clang::annotate("dtt:requires=_state == 2")]] void use();
    [[clang::annotate("dtt:requires=_state == 0")]] void useOld();
};
[[clang::annotate("dtt:requires=arg1 >= 1")]]
[[clang::annotate("dtt:ensures=arg0._state = 2")]] void advance(State&, int);
[[clang::annotate("dtt:requires=arg0 >= 1")]]
[[clang::annotate("dtt:ensures=result._value = 2")]] int produce(int);
[[clang::annotate("dtt:requires=arg0 == 2")]] void consume(int);
struct ConstState {
    mutable int _state = 0;
    [[clang::annotate("dtt:requires=arg0 >= 1")]]
    [[clang::annotate("dtt:ensures=_state = 2")]] void advance(int) const;
    [[clang::annotate("dtt:requires=_state == 0")]] void useOld() const;
};
struct QueryState {
    int _state = 0;
    [[clang::annotate("dtt:requires=arg0 >= 1")]]
    [[clang::annotate("dtt:ensures_if_true=_state == 2")]] bool ready(int) const;
    [[clang::annotate("dtt:requires=arg0 >= 1")]]
    [[clang::annotate("dtt:returns=_state")]] int value(int) const;
    [[clang::annotate("dtt:requires=_state == 2")]] void use() const;
};
[[clang::annotate("dtt:requires=arg0 == 0")]] void consumeZero(int);
[[clang::annotate("dtt:requires=arg1 >= 1")]]
[[clang::annotate("dtt:ensures=arg0._state = 2")]] void advanceConst(const State&, int);
[[clang::annotate("dtt:requires=arg1 >= 1")]]
[[clang::annotate("dtt:ensures=arg0._state = 2")]] void advancePtr(State*, int);
[[clang::annotate("dtt:ensures=arg0._state = 0")]] void initialize(State*);
#include <vector>
struct Token {};
Token *makeToken();
Token *guardedToken(int);
void poisonToken(Token*, int);
void openToken(Token*);
void useToken(Token*);
int transitionToken(Token*);
void test(int n) {
#if CASE == 0
    State s; s.advance(n);
    s.use(); // recovery-check
#elif CASE == 1
    State s; s.advance(0);
    s.use(); // recovery-check
#elif CASE == 2
    State s; s.advance(n);
    s.useOld(); // recovery-check
#elif CASE == 3
    State s; advance(s, n);
    s.use(); // recovery-check
#elif CASE == 4
    State s; advance(s, 0);
    s.useOld(); // recovery-check
#elif CASE == 5
    int result = produce(n);
    consume(result); // recovery-check
#elif CASE == 6
    int result = 2;
    result = produce(n);
    consume(result); // recovery-check
#elif CASE == 7
    State s; s.advance(1); s.use();
#elif CASE == 8
    State s; advance(s, 1); s.use();
#elif CASE == 9
    int result = produce(1); consume(result);
#elif CASE == 10
    State s;
    if (n >= 1) { s.advance(n); s.use(); }
#elif CASE == 11
    ConstState s; s.advance(n);
    s.useOld(); // recovery-check
#elif CASE == 12
    State s; advanceConst(s, n);
    s.useOld(); // recovery-check
#elif CASE == 13
    QueryState s;
    if (s.ready(n)) {
        s.use(); // unchanged-check
    }
#elif CASE == 14
    QueryState s;
    int value = s.value(n);
    consumeZero(value); // recovery-check
#elif CASE == 15
    State s; State *p = &s; initialize(p); State *alias = p;
    advancePtr(alias, n);
    p->useOld(); // recovery-check
#elif CASE == 16
    std::vector<int> v(3);
    if (n >= 0 && n < v.size()) { v.at(n); v.front(); }
#elif CASE == 17
    ConstState s; s.advance(1);
    s.useOld(); // unchanged-check
#elif CASE == 18
    QueryState s;
    if (s.ready(1)) { s.use(); }
#elif CASE == 19
    QueryState s; int value = s.value(1); consumeZero(value);
#elif CASE == 20
    Token *p = makeToken(); poisonToken(p, n);
    if (p) {
        useToken(p); // recovery-check
    }
#elif CASE == 21
    Token *p = guardedToken(n);
    if (p) {
        useToken(p); // recovery-check
    }
#elif CASE == 22
    Token t; Token *p = &t; openToken(p); Token *alias = p;
    poisonToken(alias, n);
    useToken(p); // recovery-check
#elif CASE == 23
    Token *p = guardedToken(1);
    if (p) { useToken(p); }
#elif CASE == 24
    Token t; Token *p = &t; openToken(p); Token *alias = p;
    poisonToken(p, 1);
    useToken(p); // unchanged-check
#elif CASE == 25
    Token t; Token *p = &t; openToken(p);
    int result = transitionToken(p); consume(result);
#elif CASE == 26
    if (n >= 1) {
        Token *p = guardedToken(n);
        if (p) { useToken(p); }
    }
#elif CASE == 27
    int i = produce(1);
    for (i = 0; i < 3; ++i) {
        consume(i); // recovery-check
    }
#elif CASE == 28
    int i = produce(1);
    for (i = 0; i < 3; ++i) {}
    consume(i); // recovery-check
#endif
}
