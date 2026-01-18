// 824_std_unordered_pass.cpp
// Tests: Safe std::unordered_set/unordered_map usage

#include <unordered_set>
#include <unordered_map>
#include <string>

// =============================================================================
// SECTION 1: std::unordered_set - Guard with empty()
// =============================================================================

// Variant 1.1: if (!empty()) guard
void test_uset_empty_guard() {
    std::unordered_set<int> s = {1, 2, 3};
    if (!s.empty()) {
        *s.begin();  // OK: guarded
    }
}

// Variant 1.2: early return on empty
void test_uset_early_return() {
    std::unordered_set<int> s;
    if (s.empty()) return;
    *s.begin();  // OK: guarded
}

// =============================================================================
// SECTION 2: std::unordered_set - Guard with size()
// =============================================================================

// Variant 2.1: if (size() > 0) guard
void test_uset_size_guard() {
    std::unordered_set<int> s = {1, 2, 3};
    if (s.size() > 0) {
        *s.begin();  // OK: guarded
    }
}

// =============================================================================
// SECTION 3: std::unordered_set - Insert then access
// =============================================================================

// Variant 3.1: insert then access
void test_uset_insert() {
    std::unordered_set<int> s;
    s.insert(42);
    *s.begin();  // OK: just inserted
}

// Variant 3.2: emplace then access
void test_uset_emplace() {
    std::unordered_set<int> s;
    s.emplace(42);
    *s.begin();  // OK: just emplaced
}

// =============================================================================
// SECTION 4: std::unordered_set - find() with guard
// =============================================================================

// Variant 4.1: find() != end() guard
void test_uset_find_guard() {
    std::unordered_set<int> s = {1, 2, 3};
    auto it = s.find(2);
    if (it != s.end()) {
        *it;  // OK: guarded
    }
}

// Variant 4.2: count() guard
void test_uset_count_guard() {
    std::unordered_set<int> s = {1, 2, 3};
    if (s.count(2) > 0) {
        *s.find(2);  // OK: count > 0 means element exists
    }
}

// Variant 4.3: contains() guard (C++20)
void test_uset_contains_guard() {
    std::unordered_set<int> s = {1, 2, 3};
    if (s.contains(2)) {
        *s.find(2);  // OK: contains means element exists
    }
}

// =============================================================================
// SECTION 5: std::unordered_set - Range-for
// =============================================================================

// Variant 5.1: range-for iteration
void test_uset_range_for() {
    std::unordered_set<int> s = {1, 2, 3};
    for (const auto& elem : s) {
        (void)elem;  // OK: range-for is safe
    }
}

// =============================================================================
// SECTION 6: std::unordered_map - Guard with empty()
// =============================================================================

// Variant 6.1: if (!empty()) guard
void test_umap_empty_guard() {
    std::unordered_map<int, std::string> m = {{1, "one"}, {2, "two"}};
    if (!m.empty()) {
        m.begin()->second;  // OK: guarded
    }
}

// =============================================================================
// SECTION 7: std::unordered_map - operator[]
// =============================================================================

// Variant 7.1: operator[] always succeeds
void test_umap_subscript() {
    std::unordered_map<int, std::string> m;
    m[1] = "one";  // OK: creates entry
    m[1];  // OK: entry exists
}

// =============================================================================
// SECTION 8: std::unordered_map - at() with guard
// =============================================================================

// Variant 8.1: at() with count() guard
void test_umap_at_count_guard() {
    std::unordered_map<int, std::string> m = {{1, "one"}};
    if (m.count(1) > 0) {
        m.at(1);  // OK: key exists
    }
}

// Variant 8.2: at() with find() guard
void test_umap_at_find_guard() {
    std::unordered_map<int, std::string> m = {{1, "one"}};
    if (m.find(1) != m.end()) {
        m.at(1);  // OK: key exists
    }
}

// Variant 8.3: at() with contains() guard (C++20)
void test_umap_at_contains_guard() {
    std::unordered_map<int, std::string> m = {{1, "one"}};
    if (m.contains(1)) {
        m.at(1);  // OK: key exists
    }
}

// =============================================================================
// SECTION 9: std::unordered_map - insert/emplace
// =============================================================================

// Variant 9.1: insert then at()
void test_umap_insert() {
    std::unordered_map<int, std::string> m;
    m.insert({1, "one"});
    m.at(1);  // OK: just inserted
}

// Variant 9.2: emplace then at()
void test_umap_emplace() {
    std::unordered_map<int, std::string> m;
    m.emplace(1, "one");
    m.at(1);  // OK: just emplaced
}

// Variant 9.3: insert_or_assign then at()
void test_umap_insert_or_assign() {
    std::unordered_map<int, std::string> m;
    m.insert_or_assign(1, "one");
    m.at(1);  // OK: key definitely exists
}

// Variant 9.4: try_emplace then at()
void test_umap_try_emplace() {
    std::unordered_map<int, std::string> m;
    m.try_emplace(1, "one");
    m.at(1);  // OK: key definitely exists
}

// =============================================================================
// SECTION 10: std::unordered_map - Range-for
// =============================================================================

// Variant 10.1: range-for on unordered_map
void test_umap_range_for() {
    std::unordered_map<int, std::string> m = {{1, "one"}, {2, "two"}};
    for (const auto& [key, value] : m) {
        (void)key;
        (void)value;  // OK: range-for is safe
    }
}

// =============================================================================
// SECTION 11: Bucket interface (advanced)
// =============================================================================

// Variant 11.1: bucket access with guard
void test_bucket_guard() {
    std::unordered_set<int> s = {1, 2, 3};
    size_t bucket = s.bucket(1);
    if (s.bucket_size(bucket) > 0) {
        *s.begin(bucket);  // OK: bucket has elements
    }
}

// =============================================================================
// SECTION 12: Copy/move
// =============================================================================

// Variant 12.1: copy from non-empty
void test_copy() {
    std::unordered_map<int, int> m1 = {{1, 1}};
    std::unordered_map<int, int> m2 = m1;
    m2.at(1);  // OK: copied with elements
}

int main() {
    return 0;
}
