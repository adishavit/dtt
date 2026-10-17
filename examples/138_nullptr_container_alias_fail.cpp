// Example 138: Pointer-to-container aliasing
//
// A write through a pointer to a local (pc = &c; pc->ptr = nullptr) may write
// the local. DTT does not follow what pc points to; it forgets what it knew of
// every local whose address escaped, so the dereference below is "cannot
// verify" rather than proven safe from the stale c.ptr._nonnull = 1.

struct Data {
    int value;
};

struct Container {
    Data* ptr;
};

int main() {
    Container c;
    c.ptr = new Data();   // c.ptr._nonnull = 1

    Container* pc = &c;   // pc points to c
    pc->ptr = nullptr;    // This modifies c.ptr through pc!

    c.ptr->value = 42;    // null pointer dereference  dtt-error {{null pointer dereference}}

    return 0;
}
