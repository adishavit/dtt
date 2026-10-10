// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 852_tracking_gaps_fail.cpp
// Mixed tracking limitations: only the unconstrained string parameter cases
// assert enforce-mode errors. Safe splice/loop false positives are not coverage.

#include <vector>
#include <deque>
#include <list>
#include <string>

//=============================================================================
// GAP 1: list::splice() transfers elements
// DTT cannot track element movement between containers.
//=============================================================================

void gap_splice_entire_list() {
    std::list<int> dst;
    std::list<int> src = {1, 2, 3};
    dst.splice(dst.begin(), src);
    dst.front();  // Safe (dst has 3 elements) but tracking lost
}

void gap_splice_single_element() {
    std::list<int> dst;
    std::list<int> src = {1, 2, 3};
    dst.splice(dst.begin(), src, src.begin());
    dst.front();  // Safe (dst has 1 element) but tracking lost
}

//=============================================================================
// GAP 2: string with char* pointers (unknown length at compile time)
// String literals are handled - see 861_string_literal_length_pass.cpp
// Tracked std::string sources are handled - see 865_string_tracked_buffer_pass.cpp
// But char* variables and char[] arrays have unknown strlen at compile time.
//=============================================================================

void gap_string_append_pointer(const char* str) {
    std::string s;
    s.append(str);  // Tracking lost (pointer length unknown)
    s.front();  // Safe if str non-empty, but tracking lost  // dtt-error {{_size}}
}

void gap_string_plus_equals_pointer(const char* str) {
    std::string s;
    s += str;  // Tracking lost (pointer length unknown)
    s.front();  // Safe if str non-empty, but tracking lost  // dtt-error {{_size}}
}

void gap_string_plus_equals_char_array() {
    char buf[10];
    buf[0] = 'a';
    buf[1] = 'b';
    buf[2] = '\0';  // Actual strlen is 2, but DTT can't know this

    std::string s;
    s += buf;  // Tracking lost (\0 position unknown)
    s.front();  // Safe (strlen >= 1), but tracking lost
}

//=============================================================================
// GAP 3: Loop iterations not tracked
// DTT sees one iteration, not the loop count.
//=============================================================================

template<typename Container, typename Value>
void gap_counted_loop(int count, Value val) {
    Container c;
    for (int i = 0; i < count; ++i) {
        c.push_back(val);
    }
    c[count - 1];  // Safe but DTT thinks size=1
}

void test_loop_iterations() {
    gap_counted_loop<std::vector<int>, int>(10, 0);
    gap_counted_loop<std::deque<int>, int>(5, 0);
    gap_counted_loop<std::string, char>(5, 'x');
}

void gap_while_loop() {
    std::vector<int> v;
    int count = 3;
    while (count-- > 0) {
        v.push_back(count);
    }
    v[2];  // Safe (size=3) but DTT thinks size=1
}

void gap_nested_loop() {
    std::vector<int> v;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            v.push_back(i * 3 + j);
        }
    }
    v[8];  // Safe (size=9) but DTT thinks size=1
}

//=============================================================================
// WORKAROUND: Size guards recover tracking
//=============================================================================

void workaround_size_guard() {
    std::vector<int> v;
    v.assign({1, 2, 3, 4, 5});
    if (v.size() >= 5) {
        v[4];  // OK: guard restores knowledge
    }
}

void workaround_loop_with_guard() {
    std::vector<int> v;
    for (int i = 0; i < 100; ++i) {
        v.push_back(i);
    }
    if (v.size() >= 100) {
        v[99];  // OK: guard compensates for loop limitation
    }
}

int main() { return 0; }
