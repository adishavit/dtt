// 830_std_insert_iterator_pass.cpp
// Tests insert() with iterator position - tracking is correctly maintained.

#include <vector>
#include <deque>
#include <list>

// Variant 1: vector insert with begin()
void test_vector_insert() {
    std::vector<int> v;
    v.insert(v.begin(), 42);
    v.front();  // OK: insert increments size
}

// Variant 2: deque insert with begin()
void test_deque_insert() {
    std::deque<int> d;
    d.insert(d.begin(), 42);
    d.front();  // OK: insert increments size
}

// Variant 3: list insert with begin()
void test_list_insert() {
    std::list<int> lst;
    lst.insert(lst.begin(), 42);
    lst.front();  // OK: insert increments size
}

// Variant 4: insert at end()
void test_insert_end() {
    std::vector<int> v;
    v.insert(v.end(), 42);
    v.front();  // OK: insert increments size
}

int main() { return 0; }
