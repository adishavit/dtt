// WHAT IS TESTED:
// Auto-postcondition inference for nested field modifications.
// Gap: DTT may only track direct member modifications, not nested.
//
// EXPECTED: DTT tracks modifications through nested member paths.

struct Inner {
    int value;
    int count;
};

struct Outer {
    Inner inner;
    int _size;

    [[clang::annotate("dtt:requires=inner.value > 0")]]
    int get_value() { return inner.value; }

    [[clang::annotate("dtt:requires=inner.count > 0")]]
    int get_count() { return inner.count; }

    // Gap: Nested field constant assignment
    void clear_inner() {
        inner.value = 0;
    }

    // Gap: Nested field argument assignment
    void set_inner(int v) {
        inner.value = v;
    }

    // Gap: Multiple nested fields
    void reset_inner() {
        inner.value = 0;
        inner.count = 0;
    }

    // Gap: Nested increment
    void increment_inner() {
        inner.value++;
    }
};

// Variant 1: Clear nested field
void variant1_clear() {
    Outer o;
    o.inner.value = 5;
    o.clear_inner();
    o.get_value();  // BUG: inner.value == 0
}

// Variant 2: Set nested field
void variant2_set() {
    Outer o;
    o.inner.value = 0;
    o.set_inner(10);
    o.get_value();  // OK: inner.value == 10 > 0
}

// Variant 3: Reset both nested fields
void variant3_reset_both() {
    Outer o;
    o.inner.value = 5;
    o.inner.count = 5;
    o.reset_inner();
    o.get_count();  // BUG: inner.count == 0
}

// Variant 4: Increment nested
void variant4_increment() {
    Outer o;
    o.inner.value = 0;
    o.increment_inner();
    o.get_value();  // OK: inner.value == 1 > 0
}

int main() {
    variant1_clear();
    variant2_set();
    variant3_reset_both();
    variant4_increment();
    return 0;
}
