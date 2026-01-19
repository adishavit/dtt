// 822_std_set_map_pass.cpp
// Tests: Safe std::set/std::map usage that should compile without DTT errors

#include <set>
#include <map>
#include <string>

// =============================================================================
// SECTION 1: std::set - Guard with empty() before access
// =============================================================================

// Variant 1.1: if (!empty()) guard then begin()
void test_set_empty_guard() {
    std::set<int> s = {1, 2, 3};
    if (!s.empty()) {
        *s.begin();  // OK: guarded by !empty()
    }
}

// Variant 1.2: early return on empty
void test_set_early_return() {
    std::set<int> s;
    if (s.empty()) return;
    *s.begin();  // OK: guarded by early return
}

// =============================================================================
// SECTION 2: std::set - Guard with size() check
// =============================================================================

// Variant 2.1: if (size() > 0) guard
void test_set_size_guard() {
    std::set<int> s = {1, 2, 3};
    if (s.size() > 0) {
        *s.begin();  // OK: guarded
    }
}

// =============================================================================
// SECTION 3: std::set - Access after insert
// =============================================================================

// Variant 3.1: insert then access
void test_set_insert() {
    std::set<int> s;
    s.insert(42);
    *s.begin();  // OK: just inserted, size >= 1
}

// Variant 3.2: emplace then access
void test_set_emplace() {
    std::set<int> s;
    s.emplace(42);
    *s.begin();  // OK: just emplaced
}

// =============================================================================
// SECTION 4: std::set - Initializer list and range-for
// =============================================================================

// Variant 4.1: non-empty initializer list
void test_set_init_list() {
    std::set<int> s = {1, 2, 3};
    *s.begin();  // OK: initialized with 3 elements
}

// Variant 4.2: range-for iteration
void test_set_range_for() {
    std::set<int> s = {1, 2, 3};
    for (const auto& elem : s) {
        (void)elem;  // OK: range-for is safe
    }
}

// =============================================================================
// SECTION 5: std::set - find() with guard
// =============================================================================

// Variant 5.1: find() != end() guard
void test_set_find_guard() {
    std::set<int> s = {1, 2, 3};
    auto it = s.find(2);
    if (it != s.end()) {
        *it;  // OK: guarded
    }
}

// Variant 5.2: count() guard
void test_set_count_guard() {
    std::set<int> s = {1, 2, 3};
    if (s.count(2) > 0) {
        *s.find(2);  // OK: count > 0 means element exists
    }
}

// Variant 5.3: contains() guard (C++20)
void test_set_contains_guard() {
    std::set<int> s = {1, 2, 3};
    if (s.contains(2)) {
        *s.find(2);  // OK: contains means element exists
    }
}

// =============================================================================
// SECTION 6: std::map - Guard with empty()
// =============================================================================

// Variant 6.1: if (!empty()) guard
void test_map_empty_guard() {
    std::map<int, std::string> m = {{1, "one"}, {2, "two"}};
    if (!m.empty()) {
        m.begin()->second;  // OK: guarded
    }
}

// =============================================================================
// SECTION 7: std::map - operator[] creates entry
// =============================================================================

// Variant 7.1: operator[] always succeeds (creates if missing)
void test_map_subscript() {
    std::map<int, std::string> m;
    m[1] = "one";  // OK: creates entry
    m[1];  // OK: entry exists now
}

// Variant 7.2: operator[] on existing key
void test_map_subscript_existing() {
    std::map<int, std::string> m = {{1, "one"}};
    m[1];  // OK: key exists
}

// =============================================================================
// SECTION 8: std::map - at() with guard
// =============================================================================

// Variant 8.1: at() with count() guard
void test_map_at_count_guard() {
    std::map<int, std::string> m = {{1, "one"}, {2, "two"}};
    if (m.count(1) > 0) {
        m.at(1);  // OK: key exists
    }
}

// Variant 8.2: at() with find() guard
void test_map_at_find_guard() {
    std::map<int, std::string> m = {{1, "one"}};
    if (m.find(1) != m.end()) {
        m.at(1);  // OK: key exists
    }
}

// Variant 8.3: at() with contains() guard (C++20)
void test_map_at_contains_guard() {
    std::map<int, std::string> m = {{1, "one"}};
    if (m.contains(1)) {
        m.at(1);  // OK: key exists
    }
}

// =============================================================================
// SECTION 9: std::map - insert/emplace
// =============================================================================

// Variant 9.1: insert then access
void test_map_insert() {
    std::map<int, std::string> m;
    m.insert({1, "one"});
    m.at(1);  // OK: just inserted
}

// Variant 9.2: emplace then access
void test_map_emplace() {
    std::map<int, std::string> m;
    m.emplace(1, "one");
    m.at(1);  // OK: just emplaced
}

// Variant 9.3: insert_or_assign then access
void test_map_insert_or_assign() {
    std::map<int, std::string> m;
    m.insert_or_assign(1, "one");
    m.at(1);  // OK: key definitely exists
}

// =============================================================================
// SECTION 10: std::map - Range-for
// =============================================================================

// Variant 10.1: range-for on map
void test_map_range_for() {
    std::map<int, std::string> m = {{1, "one"}, {2, "two"}};
    for (const auto& [key, value] : m) {
        (void)key;
        (void)value;  // OK: range-for is safe
    }
}

// =============================================================================
// SECTION 11: std::multiset/std::multimap
// =============================================================================

// Variant 11.1: multiset empty guard
void test_multiset_empty_guard() {
    std::multiset<int> ms = {1, 1, 2, 2, 3};
    if (!ms.empty()) {
        *ms.begin();  // OK: guarded
    }
}

// Variant 11.2: multimap empty guard
void test_multimap_empty_guard() {
    std::multimap<int, std::string> mm = {{1, "a"}, {1, "b"}};
    if (!mm.empty()) {
        mm.begin()->second;  // OK: guarded
    }
}

// =============================================================================
// SECTION 12: Copy/move preserves elements
// =============================================================================

// Variant 12.1: copy map
void test_map_copy() {
    std::map<int, int> m1 = {{1, 1}, {2, 2}};
    std::map<int, int> m2 = m1;
    m2.at(1);  // OK: copied with elements
}

int main() {
    return 0;
}
