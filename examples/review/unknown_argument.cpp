// Isolated argument-contract and overwrite regressions. Select with -DCASE=N.
int getValue();
struct Value {
    int len = 0;
    [[clang::annotate("dtt:requires=arg0 >= 20")]] void needsTwenty(int = 20);
    [[clang::annotate("dtt:requires=len >= 20")]] void needsFieldTwenty();
};

void test(int input) {
    Value a, b, c;
#if CASE == 0
    a.needsTwenty(19); // Proven violation in every mode.
#elif CASE == 1
    a.needsTwenty(20); // Proven safe.
#elif CASE == 2
    a.len = 19;
    a.needsTwenty(a.len); // Proven violation, currently skipped.
#elif CASE == 3
    a.len = 20;
    a.needsTwenty(a.len); // Proven safe.
#elif CASE == 4
    a.needsTwenty(input); // Unknown: Category D, not a proven violation.
#elif CASE == 5
    if (input >= 20) a.needsTwenty(input); // Guard proves safety.
#elif CASE == 6
    a.len = getValue();
    b.len = a.len + 5;
    c.len = b.len + 10;
    c.needsTwenty(c.len); // Unknown: Category D.
#elif CASE == 7
    a.len = getValue();
    b.len = a.len + 5;
    c.len = b.len + 10;
    if (a.len >= 5) c.needsTwenty(c.len); // Symbolic guarded chain.
#elif CASE == 8
    a.len = getValue();
    a.needsFieldTwenty(); // Unknown; the old initializer must not survive.
#elif CASE == 9
    a.len = 20;
    a.len = getValue();
    a.needsFieldTwenty(); // Unknown; explicit assignment must not survive.
#elif CASE == 10
    a.needsTwenty(input + 20); // Unknown expression: Category D.
#elif CASE == 11
    if (input >= 0) a.needsTwenty(input + 20); // Guarded arithmetic.
#elif CASE == 12
    a.len = input;
    if (a.len >= 20) a.needsTwenty(a.len); // Guard on a member.
#elif CASE == 13
    a.needsTwenty(); // Default argument is the known safe value 20.
#elif CASE == 14
    // Independent unknown calls must not collapse into the same solver variable.
    a.needsTwenty(getValue() - getValue() + 20);
#elif CASE == 15
    // C++ signed remainder can be negative; Z3 integer modulo is nonnegative.
    a.needsTwenty(input % 3 + 20);
#elif CASE == 16
    int n = 19;
    n = getValue();
    a.needsTwenty(n); // Unknown, not a proven violation from the stale 19.
#elif CASE == 17
    int n = 20;
    n = getValue();
    a.needsTwenty(n); // Unknown, not proven safe from the stale 20.
#elif CASE == 18
    if (input >= 20) {
        input = getValue();
        a.needsTwenty(input); // The old guard must not survive assignment.
    }
#elif CASE == 19
    unsigned n = static_cast<unsigned>(input);
    a.needsTwenty(n + 20u); // Unsigned wrap and conversion are not integer algebra.
#elif CASE == 20
    int i;
    for (i = 20; i < 30; i = i + 1) a.needsTwenty(i);
#elif CASE == 21
    for (int i = 20; i < 30; ++i) a.needsTwenty(i);
#elif CASE == 22
    int i;
    for (i = 20; i < 30; i = i + 1) a.needsTwenty(i - 20);
#endif
}
