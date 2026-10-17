// 1755: Null checks against a cast zero - the null branch is still null
#define C_NULL ((void *)0)

// Variant 1: dereference on the branch where p is C's NULL.
int variant1_deref_when_null(int *p) {
    if (p != C_NULL) return 0;
    return *p;  // dtt-error {{known to be null}}
}

int main() { return 0; }
