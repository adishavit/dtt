// Return value property tracking — should COMPILE but currently FAILS
//
// WHAT IS TESTED:
// When a function returns an object, the returned object should carry
// properties established by the function's postconditions.
//
// WHY IT'S DIFFICULT:
// 1. Return value is a temporary - needs lifetime tracking
// 2. Must bind postconditions to the result, not 'this' or parameters
// 3. Copy/move elision complicates tracking
// 4. Chained calls: foo().bar().baz() - each return needs tracking
//
// RATIONALE:
// Factory functions and builders return objects with guarantees:
// - vector::reserve(n) returns *this with capacity >= n
// - make_unique<T>() returns non-null
// - string::substr() returns string with size <= original
//
// CURRENT BEHAVIOR:
// dtt:ensures establishes postconditions, but these are checked at
// return statements, not propagated to callers. The caller sees
// the return value as having unknown properties.

struct Builder {
    int* data;
    int _size;
    int _capacity;

    // Returns *this after reserving space
    // Postcondition: _capacity >= n
    [[clang::annotate("dtt:ensures=_capacity >= arg0")]]
    Builder& reserve(int n) {
        // ... allocation logic ...
        return *this;
    }
};

[[clang::annotate("dtt:requires=_capacity >= 100")]]
void needsCapacity(Builder& b);

int main() {
    Builder b;

    // reserve(100) has postcondition: _capacity >= 100
    b.reserve(100);

    // XFAIL: Postcondition from reserve() not tracked on caller side
    needsCapacity(b);  // Should PASS: reserve guarantees _capacity >= 100

    // Chained case is even harder:
    Builder c;
    // c.reserve(50).reserve(100) - final capacity should be >= 100
    // But intermediate states are temporaries

    return 0;
}
