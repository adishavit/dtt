// 860_std_map_at_empty_fail.cpp
// Tests: map::at() on empty map - DTT catches this

#include <map>
#include <unordered_map>
#include <string>

// Variant 1: at() on default-constructed map
void test_map_at_empty() {
    std::map<int, std::string> m;
    m.at(1);  // ERROR: map is empty
}

// Variant 2: at() on cleared map
void test_map_at_after_clear() {
    std::map<int, std::string> m = {{1, "one"}};
    m.clear();
    m.at(1);  // ERROR: map is empty after clear
}

// Variant 3: at() on default-constructed unordered_map
void test_umap_at_empty() {
    std::unordered_map<int, std::string> m;
    m.at(1);  // ERROR: map is empty
}

// Variant 4: at() on cleared unordered_map
void test_umap_at_after_clear() {
    std::unordered_map<int, std::string> m = {{1, "one"}};
    m.clear();
    m.at(1);  // ERROR: map is empty after clear
}

int main() { return 0; }
