// WHAT IS TESTED:
// Complex method chaining in postcondition inference.
// Tests: long chains, mixed operations, state accumulation.
//
// EXPECTED: DTT tracks state through entire method chain.

struct Builder {
    int _size;
    int _capacity;
    int* data;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:requires=_size > 9")]]
    int& at9() { return data[9]; }

    [[clang::annotate("dtt:requires=_size >= n")]]
    void require_n(int n) {}

    // Chain returns: each returns *this
    Builder& clear() {
        _size = 0;
        return *this;
    }

    Builder& resize(int n) {
        _size = n;
        return *this;
    }

    Builder& push() {
        _size++;
        return *this;
    }

    Builder& pop() {
        _size--;
        return *this;
    }

    Builder& reserve(int n) {
        _capacity = n;
        return *this;
    }

    Builder& fill() {
        _size = _capacity;
        return *this;
    }

    Builder& double_size() {
        _size = _size * 2;
        return *this;
    }

    Builder& add(int n) {
        _size += n;
        return *this;
    }
};

// Variant 1: Long chain of pushes
void variant1_many_push() {
    Builder b;
    b._size = 0;
    b.push().push().push().push().push()
     .push().push().push().push().push();  // 10 pushes
    b.at9();  // OK: _size == 10 > 9
}

// Variant 2: Long chain insufficient
void variant2_many_push_insufficient() {
    Builder b;
    b._size = 0;
    b.push().push().push().push().push()
     .push().push().push().push();  // 9 pushes
    b.at9();  // BUG: _size == 9, need > 9
}

// Variant 3: Clear then build up
void variant3_clear_build() {
    Builder b;
    b._size = 100;
    b.clear().push().push().push().push().push()
             .push().push().push().push().push();  // clear then 10 pushes
    b.at9();  // OK: _size == 10 > 9
}

// Variant 4: Build then clear
void variant4_build_clear() {
    Builder b;
    b._size = 0;
    b.push().push().push().push().push()
     .push().push().push().push().push()
     .clear();  // 10 pushes then clear
    b.front();  // BUG: _size == 0
}

// Variant 5: Reserve and fill
void variant5_reserve_fill() {
    Builder b;
    b._size = 0;
    b._capacity = 0;
    b.reserve(10).fill();  // _capacity = 10, _size = 10
    b.at9();  // OK: _size == 10 > 9
}

// Variant 6: Reserve and fill unsafe
void variant6_reserve_fill_unsafe() {
    Builder b;
    b._size = 0;
    b._capacity = 0;
    b.reserve(5).fill();  // _capacity = 5, _size = 5
    b.at9();  // BUG: _size == 5 <= 9
}

// Variant 7: Double size chain
void variant7_double_chain() {
    Builder b;
    b._size = 1;
    b.double_size().double_size().double_size().double_size();  // 1 -> 2 -> 4 -> 8 -> 16
    b.at9();  // OK: _size == 16 > 9
}

// Variant 8: Double insufficient
void variant8_double_insufficient() {
    Builder b;
    b._size = 1;
    b.double_size().double_size().double_size();  // 1 -> 2 -> 4 -> 8
    b.at9();  // BUG: _size == 8 <= 9
}

// Variant 9: Mixed operations
void variant9_mixed() {
    Builder b;
    b._size = 5;
    b.push().push().push().double_size().pop();  // 5 + 3 = 8, * 2 = 16, - 1 = 15
    b.at9();  // OK: _size == 15 > 9
}

// Variant 10: Mixed operations unsafe
void variant10_mixed_unsafe() {
    Builder b;
    b._size = 2;
    b.push().push().pop().double_size();  // 2 + 2 - 1 = 3, * 2 = 6
    b.at9();  // BUG: _size == 6 <= 9
}

// Variant 11: Add chain
void variant11_add_chain() {
    Builder b;
    b._size = 0;
    b.add(3).add(3).add(4);  // 0 + 3 + 3 + 4 = 10
    b.at9();  // OK: _size == 10 > 9
}

// Variant 12: Complex chain with clear in middle
void variant12_clear_middle() {
    Builder b;
    b._size = 100;
    b.push().push().clear().push().push();  // ignore first, clear, 2 pushes
    b.at9();  // BUG: _size == 2 <= 9
}

int main() {
    variant1_many_push();
    variant2_many_push_insufficient();
    variant3_clear_build();
    variant4_build_clear();
    variant5_reserve_fill();
    variant6_reserve_fill_unsafe();
    variant7_double_chain();
    variant8_double_insufficient();
    variant9_mixed();
    variant10_mixed_unsafe();
    variant11_add_chain();
    variant12_clear_middle();
    return 0;
}
