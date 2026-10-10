// Tests: Placement new should not be deleted
// Issue #56: AllocSource::Placement is defined but never set
// Gap: DTT marks placement new as AllocSource::New, missing the delete error

#include <new>

struct Foo {
    int x;
    Foo() : x(42) {}
    ~Foo() {}
};

// Variant 1: Basic placement new with delete (UB)
void variant1_placement_delete() {
    alignas(Foo) char buffer[sizeof(Foo)];
    Foo* p = new (buffer) Foo();  // Placement new - no allocation
    delete p;  // gap: UB - deleting stack memory, DTT should error but doesn't  dtt-error {{mismatched deallocation}}
}

// Variant 2: Placement new array with delete[] (UB)
void variant2_placement_array_delete() {
    alignas(Foo) char buffer[sizeof(Foo) * 3];
    Foo* p = new (buffer) Foo[3];  // Placement new array
    delete[] p;  // gap: UB - deleting stack memory, DTT should error but doesn't  dtt-error {{mismatched deallocation}}
}

// Variant 3: nothrow new is NOT placement new - delete is OK
void variant3_nothrow_new_ok() {
    Foo* p = new (std::nothrow) Foo();  // nothrow new - still allocates
    if (p) {
        delete p;  // OK - this is regular allocation, just nullable
    }
}

int main() {
    // Don't actually run - these are UB
    // variant1_placement_delete();
    // variant2_placement_array_delete();
    variant3_nothrow_new_ok();  // This one is safe
    return 0;
}
