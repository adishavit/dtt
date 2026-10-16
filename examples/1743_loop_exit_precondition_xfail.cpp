// 1743: Loops that cannot exit for some values of their variable (DTT gaps)
//
// Each loop below never exits for some inputs, but DTT does not report it.
// Rename to _fail (with dtt-error markers) when DTT reports these.

// Variant 1: the loop variable is written through a pointer. Variables whose
// address is taken are not analyzed.
int variant1_alias() {
    int x = -1;
    int *p = &x;
    int r = 0;
    while (x) {
        *p >>= 1;  // BUG: x stays -1
        ++r;
    }
    return r;
}

// Variant 2: the condition compares two variables; for even n, i stays odd
// and wraps around forever. Only `variable OP constant` conditions are
// analyzed.
int variant2_parity() {
    unsigned n = 4;
    int r = 0;
    for (unsigned i = 1; i != n; i += 2)  // BUG: never exits
        ++r;
    return r;
}

// Variant 3: the update is a bitwise operation. The solver's integers do not
// model bitwise operations.
int variant3_bitwise() {
    unsigned u = 2;
    int r = 0;
    while (u) {
        u |= 1;  // BUG: u stays nonzero
        ++r;
    }
    return r;
}

// Variant 4: the loop variable is a struct field. Only locals and
// parameters are analyzed.
struct Counter { int v; };
int variant4_field() {
    Counter c{-1};
    int r = 0;
    while (c.v) {
        c.v >>= 1;  // BUG: c.v stays -1
        ++r;
    }
    return r;
}

int main() { return 0; }
