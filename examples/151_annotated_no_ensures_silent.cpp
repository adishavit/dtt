// SILENT: Annotated method without ensures — false negative (by design)
//
// DESIGN LIMITATION:
// DTT trusts methods that have ANY annotation (requires, ensures, etc.).
// If a user adds `requires` but forgets `ensures` on a mutating method,
// DTT won't detect the mutation and will trust stale property values.
//
// RATIONALE:
// The same limitation exists for free functions. If a user has taken the
// time to annotate a method with `requires`, we assume they're actively
// using DTT and would add `ensures` if the method mutated tracked fields.
// Automatically invalidating would cause false positives on accessor
// methods that have `requires` but legitimately don't change anything.
//
// WORKAROUND:
// Always add `ensures` to methods that mutate tracked fields:
//   [[clang::annotate("dtt:ensures=_size == 0")]]
//   void clear();

struct Vec {
    int* data;
    int _size;

    // clear() has a precondition but NO postcondition
    // It actually sets _size = 0, but DTT doesn't know that
    [[clang::annotate("dtt:requires=_size >= 0")]]
    void clear();  // Non-const, mutates _size, but no ensures!
};

[[clang::annotate("dtt:requires=_size > 0")]]
void needsNonEmpty(Vec& v);

void test() {
    int arr[] = {1,2,3,4,5,6,7,8,9,10};
    Vec v;
    v.data = arr;
    v._size = 10;  // Known: v._size = 10

    v.clear();  // DTT trusts annotated methods, doesn't invalidate
                // clear() actually sets _size = 0

    // Should FAIL but PASSES: DTT still thinks v._size = 10
    needsNonEmpty(v);
}

int main() { test(); return 0; }
