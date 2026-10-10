// Direct annotation consumers must honor the same call-entry contract.
struct Query {
    int _state = 0;
    [[clang::annotate("dtt:requires=arg0 >= 1")]]
    [[clang::annotate("dtt:returns=_state")]] int value(int) const;
    [[clang::annotate("dtt:requires=_state == 2")]] void use() const;
    [[clang::annotate("dtt:requires=arg0 < _state")]] int operator[](int) const;
};
struct Factory {
    [[clang::annotate("dtt:requires=arg0 >= 1")]]
    [[clang::annotate("dtt:ensures=result._value = 2")]] static int produce(int);
};
struct ConstState {
    mutable int _state = 0;
    [[clang::annotate("dtt:requires=arg0 >= 1")]]
    [[clang::annotate("dtt:ensures=_state = 2")]] void operator+=(int) const;
    [[clang::annotate("dtt:requires=_state == 0")]] void useOld() const;
};
[[clang::annotate("dtt:requires=arg0 == 2")]] void consume(int);
void test(int n, int i) {
#if CASE == 0
    Query q;
    if (q.value(n) == 2)
        q.use(); // edge-check
#elif CASE == 1
    Query q;
    if (i < q.value(n))
        q[i]; // edge-check
#elif CASE == 2
    Query q;
    if (q.value(n) > i)
        q[i]; // edge-check
#elif CASE == 3
    Query q;
    for (int j = 0; j < q.value(n); ++j)
        q[j]; // edge-check
#elif CASE == 4
    int value = Factory::produce(n);
    consume(value); // edge-check
#elif CASE == 5
    ConstState s; s += n;
    s.useOld(); // edge-check
#elif CASE == 6
    Query q;
    if (q.value(1) == 2) q.use();
#elif CASE == 7
    int value = Factory::produce(1); consume(value);
#elif CASE == 8
    ConstState s; s += 1;
    s.useOld(); // proven-check
#elif CASE == 9
    Query q;
    if (i < q.value(1)) q[i];
#elif CASE == 10
    Query q;
    if (q.value(1) > i) q[i];
#elif CASE == 11
    Query q;
    for (int j = 0; j < q.value(1); ++j) q[j];
#endif
}
