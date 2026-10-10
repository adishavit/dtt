// WHAT IS TESTED:
// Auto-postcondition inference for return values.
// Gap: DTT doesn't track that a function's return value equals a field.
//
// EXPECTED: DTT infers return value properties for use at call site.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    // Gap: Return value equals field
    int size() const { return _size; }

    // Gap: Return value is computed
    int size_minus_one() const { return _size - 1; }

    // Gap: Return boolean condition
    bool empty() const { return _size == 0; }

    // Gap: Pop returns old value
    int pop() {
        return data[--_size];
    }

    void clear() { _size = 0; }
};

// Free function that uses return value
[[clang::annotate("dtt:requires=n > 0")]]
void process(int n) { /* uses n */ }

// Variant 1: Use size() return value
void variant1_size_return() {
    Vec v;
    v._size = 5;
    int s = v.size();
    process(s);  // OK: s == 5 > 0
}

// Variant 2: Size after clear
void variant2_size_after_clear() {
    Vec v;
    v._size = 5;
    v.clear();
    int s = v.size();
    process(s);  // BUG: s == 0  dtt-error {{arg0 > 0}}
}

// Variant 3: Computed return
void variant3_computed() {
    Vec v;
    v._size = 5;
    int s = v.size_minus_one();
    process(s);  // OK: s == 4 > 0
}

// Variant 4: Pop side effect
void variant4_pop_effect() {
    Vec v;
    v._size = 1;
    v.pop();      // _size becomes 0
    v.front();    // BUG: _size == 0  dtt-error {{_size > 0}}
}

// Variant 5: Boolean return for guard
void variant5_empty_guard() {
    Vec v;
    v._size = 0;
    if (!v.empty()) {
        v.front();  // Should be OK if guard understood
    }
}

int main() {
    variant1_size_return();
    variant2_size_after_clear();
    variant3_computed();
    variant4_pop_effect();
    variant5_empty_guard();
    return 0;
}
