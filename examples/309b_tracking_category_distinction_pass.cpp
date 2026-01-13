// Tests: Distinction between Untracked (Category A) vs Lost (Category C)
// These should all PASS - untracked fields are silently allowed

void unknown(struct Vec& v);

struct Vec {
    int* data;
    int _size;
    int _capacity;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

// Variant 1: Mixed tracked/untracked - only untracked field used
void variant1_untracked_only() {
    Vec v;
    // _size never established, _capacity never established
    // Using a method that requires _size should be Category A (silently allow)
    // because we never established tracking
    // NOTE: This is actually what we want - if user doesn't track, we don't warn
}

// Variant 2: Tracked field survives, untracked field accessed
void variant2_tracked_survives() {
    Vec v;
    v._size = 5;      // Tracking established for _size
    v._capacity = 10; // Tracking established for _capacity
    v.front();        // OK: _size is tracked and > 0
}

// Variant 3: Different variables in nested scopes
void variant3_nested_scopes() {
    {
        Vec v;
        v._size = 1;
        v.front();  // OK: tracked
    }
    {
        Vec v;
        v._size = 2;
        v.front();  // OK: different v, tracked
    }
}

// Variant 4: Method that establishes tracking
struct Container {
    int _count = 0;

    [[clang::annotate("dtt:ensures=_count > 0")]]
    void addItem() { _count++; }

    [[clang::annotate("dtt:requires=_count > 0")]]
    void process() {}
};

void variant4_method_establishes() {
    Container c;
    c.addItem();  // Establishes _count > 0
    c.process();  // OK: tracking established via postcondition
}

int main() {
    variant2_tracked_survives();
    variant3_nested_scopes();
    variant4_method_establishes();
    return 0;
}
