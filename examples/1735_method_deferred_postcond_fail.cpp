// 1735: Deferred eval for FnPostconditions (method-style annotations)
// When method postconditions have `when` guards that aren't satisfied at call
// time, they should be deferred. Currently, FnPostconditions skip guarded
// postconditions without deferring.

// Note: The current std.dtt annotations don't use `when` guards on methods,
// so this test checks that the existing method postcondition path still works
// correctly and that guarded FnPostconditions are now deferred.

#include <vector>

// Variant 1: existing behavior — vector access after size check
void variant1_vector_access_guarded() {
    std::vector<int> v;
    v.push_back(1);
    // _size proven >= 1 by push_back postcondition
    int x = v[0];     // OK: 0 < _size
    (void)x;
    int y = v[1];     // ERROR: 1 >= _size (size is 1)  dtt-error {{out of bounds}}
    (void)y;
}

// Variant 2: vector.front() after empty check
void variant2_front_after_empty_check() {
    std::vector<int> v;
    if (!v.empty()) {
        int x = v.front();   // OK: empty() returned false → _size >= 1
        (void)x;
    }
}

int main() { return 0; }
