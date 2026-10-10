// WHAT IS TESTED:
// Auto-postcondition inference when 'this' is aliased.
//
// EXPECTED: DTT tracks modifications through aliases.
// RESULT: DTT correctly catches all bugs (via null-check side effect).

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    // Gap: Alias 'this' and modify
    void clear_via_alias() {
        Vec* self = this;
        self->_size = 0;
    }

    // Gap: Reference to 'this'
    void clear_via_ref() {
        Vec& self = *this;
        self._size = 0;
    }

    // Gap: Modify via helper with alias
    void modify_via_helper() {
        Vec* p = this;
        helper_clear(p);
    }

    static void helper_clear(Vec* v) {
        v->_size = 0;
    }
};

// Variant 1: Clear via pointer alias
void variant1_ptr_alias() {
    Vec v;
    v._size = 5;
    v.clear_via_alias();
    v.front();  // BUG: _size == 0  dtt-error {{_size > 0}}
}

// Variant 2: Clear via reference alias
void variant2_ref_alias() {
    Vec v;
    v._size = 5;
    v.clear_via_ref();
    v.front();  // BUG: _size == 0  dtt-error {{_size > 0}}
}

// Variant 3: Clear via helper
void variant3_helper() {
    Vec v;
    v._size = 5;
    v.modify_via_helper();
    v.front();  // BUG: _size == 0  dtt-error {{_size > 0}}
}

int main() {
    variant1_ptr_alias();
    variant2_ref_alias();
    variant3_helper();
    return 0;
}
