// dtt-run: enforce
// Example 144: Pointer nullability through member-pointer copies
//
// A pointer copied from a member (`Data* p = c.ptr;` or `p = c.ptr;`) takes
// the member's tracked properties, the same way `Data* p = q;` does. Once
// c.ptr is known non-null, dereferencing the copy needs no extra check.
//
// Previously the copy lost tracking and every dereference was reported as
// "nullability unknown" (a false positive this file used to assert).
// Pointers returned by unannotated calls or read from arrays are still untracked.

struct Data {
    int value;
};

struct Container {
    Data* ptr;
};

int main() {
    // CASE 1: Copy from member pointer
    Container c;
    c.ptr = new Data();     // c.ptr._nonnull = 1
    Data* p1 = c.ptr;       // p1._nonnull = 1 (copied from c.ptr)
    p1->value = 10;         // OK

    // CASE 2: Assignment from member pointer
    Data* p2 = nullptr;
    p2 = c.ptr;             // p2._nonnull = 1 (copied from c.ptr)
    p2->value = 20;         // OK

    // CASE 3: Direct member access, then a copy
    c.ptr->value = 30;      // OK: c.ptr._nonnull = 1
    Data* p3 = c.ptr;       // p3._nonnull = 1 (copied from c.ptr)
    p3->value = 40;         // OK

    return 0;
}
