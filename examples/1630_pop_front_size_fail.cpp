// 1630: pop_front() should decrement _size
// Same bug class as adapter pop() (1600): uses addPre instead of addPop.
// After push+pop_front, _size remains 1 instead of 0.

#include <deque>
#include <list>
#include <forward_list>

// Variant 1: deque push_back + pop_front + front
void variant1_deque_pop_front() {
    std::deque<int> d;
    d.push_back(42);  // _size = 1
    d.pop_front();    // State change checked by the subsequent requirement assertion.
    d.front();        // ERROR: _size should be 0  dtt-error {{_size > 0}}
}

// Variant 2: list push_back + pop_front + front
void variant2_list_pop_front() {
    std::list<int> l;
    l.push_back(42);  // _size = 1
    l.pop_front();    // State change checked by the subsequent requirement assertion.
    l.front();        // ERROR: _size should be 0  dtt-error {{_size > 0}}
}

// Variant 3: forward_list push_front + pop_front + front
void variant3_forward_list_pop_front() {
    std::forward_list<int> fl;
    fl.push_front(42);  // _size = 1
    fl.pop_front();     // State change checked by the subsequent requirement assertion.
    fl.front();         // ERROR: _size should be 0  dtt-error {{_size > 0}}
}

// Variant 4: multiple push + pop_front drains to empty
void variant4_deque_drain_via_pop_front() {
    std::deque<int> d;
    d.push_back(1);   // _size = 1
    d.push_back(2);   // _size = 2
    d.pop_front();    // _size should be 1
    d.pop_front();    // _size should be 0
    d.front();        // ERROR: _size should be 0  dtt-error {{_size > 0}}
}

int main() { return 0; }
