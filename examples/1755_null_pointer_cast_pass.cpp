// 1755: Null checks against a cast zero - C's NULL is ((void *)0)
//
// DTT recognized `p == NULL` only for C++'s NULL (__null) and literal 0, so
// every C null check against NULL proved nothing (zlib gz_open).
#define C_NULL ((void *)0)

struct st { int mode; };

// Variant 1: early return on a C NULL comparison.
int variant1_c_null(int *p) {
    if (p == C_NULL) return 0;
    return *p;
}

// Variant 2: the comparison written the other way round.
int variant2_reversed(int *p) {
    if (C_NULL == p) return 0;
    return *p;
}

// Variant 3: a typed null pointer.
int variant3_typed_null(st *s) {
    if (s == (st *)0) return 0;
    return s->mode;
}

int main() { return 0; }
