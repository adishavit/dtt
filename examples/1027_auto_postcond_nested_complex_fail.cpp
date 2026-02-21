// WHAT IS TESTED:
// Complex nested field modifications in postcondition inference.
// Tests: multi-level nesting, array of structs, cross-references.
//
// EXPECTED: DTT tracks modifications through nested member paths.

struct Stats {
    int count;
    int sum;
    int max;
};

struct Buffer {
    int* data;
    int _size;
    Stats stats;
    Stats* pstats;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:requires=stats.count > 0")]]
    int average() { return stats.sum / stats.count; }

    [[clang::annotate("dtt:requires=stats.count > n")]]
    void require_count_gt(int n) {}

    // Nested field constant assignment
    void clear_count() {
        stats.count = 0;
    }

    // Nested field arithmetic
    void inc_count() {
        stats.count++;
    }

    // Nested field from argument
    void set_count(int n) {
        stats.count = n;
    }

    // Cross-field: nested = top-level
    void sync_count_to_size() {
        stats.count = _size;
    }

    // Multiple nested fields
    void reset_stats() {
        stats.count = 0;
        stats.sum = 0;
        stats.max = 0;
    }

    // Computed nested field
    void update_stats(int value) {
        stats.count++;
        stats.sum += value;
        if (value > stats.max) stats.max = value;
    }
};

struct Container {
    Buffer bufs[2];
    int active;

    [[clang::annotate("dtt:requires=bufs[0]._size > 0")]]
    int& first_front() { return bufs[0].data[0]; }

    [[clang::annotate("dtt:requires=bufs[1]._size > 0")]]
    int& second_front() { return bufs[1].data[0]; }

    // Indexed nested modification
    void clear_first() {
        bufs[0]._size = 0;
    }

    // Indexed nested from argument
    void set_first_size(int n) {
        bufs[0]._size = n;
    }
};

// Variant 1: Clear nested count
void variant1_clear_nested() {
    Buffer b;
    b.stats.count = 5;
    b.clear_count();
    b.average();  // BUG: stats.count == 0, division by zero
}

// Variant 2: Increment nested count
void variant2_inc_nested() {
    Buffer b;
    b.stats.count = 0;
    b.inc_count();
    b.average();  // OK: stats.count == 1
}

// Variant 3: Set nested from argument
void variant3_set_nested() {
    Buffer b;
    b.stats.count = 0;
    b.set_count(5);
    b.require_count_gt(4);  // OK: stats.count == 5 > 4
}

// Variant 4: Set nested unsafe
void variant4_set_nested_unsafe() {
    Buffer b;
    b.stats.count = 10;
    b.set_count(3);
    b.require_count_gt(4);  // BUG: stats.count == 3 <= 4
}

// Variant 5: Cross-field sync
void variant5_cross_field() {
    Buffer b;
    b._size = 10;
    b.stats.count = 0;
    b.sync_count_to_size();
    b.require_count_gt(5);  // OK: stats.count == 10 > 5
}

// Variant 6: Cross-field sync unsafe
void variant6_cross_field_unsafe() {
    Buffer b;
    b._size = 3;
    b.stats.count = 10;
    b.sync_count_to_size();
    b.require_count_gt(5);  // BUG: stats.count == 3 <= 5
}

// Variant 7: Reset all nested
void variant7_reset_all() {
    Buffer b;
    b.stats.count = 10;
    b.reset_stats();
    b.average();  // BUG: stats.count == 0
}

// Variant 8: Indexed nested clear
void variant8_indexed_clear() {
    Container c;
    c.bufs[0]._size = 10;
    c.clear_first();
    c.first_front();  // BUG: bufs[0]._size == 0
}

// Variant 9: Indexed nested set
void variant9_indexed_set() {
    Container c;
    c.bufs[0]._size = 0;
    c.set_first_size(10);
    c.first_front();  // OK: bufs[0]._size == 10
}

int main() {
    variant1_clear_nested();
    variant2_inc_nested();
    variant3_set_nested();
    variant4_set_nested_unsafe();
    variant5_cross_field();
    variant6_cross_field_unsafe();
    variant7_reset_all();
    variant8_indexed_clear();
    variant9_indexed_set();
    return 0;
}
