// 1610: at() bounds checking for sequence containers - DTT gap
// operator[] has bounds checking but at() does not (missing annotation).
// Both should check index < _size.

#include <vector>
#include <deque>
#include <string>
#include <string_view>
#include <array>

// Variant 1: vector::at() on empty vector
void variant1_vector_at_empty() {
    std::vector<int> v;
    v.at(0);  // ERROR: _size == 0, 0 < 0 is false  dtt-error {{out of bounds}}
}

// Variant 2: deque::at() on empty deque
void variant2_deque_at_empty() {
    std::deque<int> d;
    d.at(0);  // ERROR: _size == 0  dtt-error {{out of bounds}}
}

// Variant 3: string::at() on empty string
void variant3_string_at_empty() {
    std::string s;
    s.at(0);  // ERROR: _size == 0  dtt-error {{out of bounds}}
}

// Variant 4: string_view::at() on empty string_view
void variant4_string_view_at_empty() {
    std::string_view sv;
    sv.at(0);  // ERROR: _size == 0  dtt-error {{out of bounds}}
}

// Variant 5: array::at() out of bounds
void variant5_array_at_oob() {
    std::array<int, 3> a = {1, 2, 3};
    a.at(5);  // ERROR: 5 < 3 is false  dtt-error {{out of bounds}}
}

int main() { return 0; }
