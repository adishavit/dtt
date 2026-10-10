// Compile each CASE independently: another error cannot hide a missed guard.
struct GContainer {
    GContainer();
    void prepare();
    void add(int);
    [[clang::annotate("dtt:requires=_size == 0")]] void require_empty();
    [[clang::annotate("dtt:requires=_size > 0")]] void require_nonempty();
};

void example() {
    GContainer c;
#if CASE == 0
    c.add(1);
    c.require_empty();
#elif CASE == 1
    c.add(1);
    c.add(2);
    c.require_empty();
#elif CASE == 2
    c.prepare();
    c.add(1);
    c.require_nonempty();
#elif CASE == 3
    c.add(1);
    c.require_nonempty(); // Must diagnose: false guard cannot establish size.
#elif CASE == 4
    c.prepare();
    c.add(1);
    c.require_empty(); // Must diagnose: true guard must update size.
#endif
}
