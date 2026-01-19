// 866_initializer_list_size_pass.cpp
// Tests assign() with initializer_list size detection

#include <vector>
#include <deque>
#include <list>
#include <string>

void test_vector_assign_init_list() {
    std::vector<int> v;
    v.assign({1, 2, 3, 4, 5});  // _size = 5
    v[4];  // OK: index 4 < size 5
}

void test_deque_assign_init_list() {
    std::deque<int> d;
    d.assign({10, 20, 30});  // _size = 3
    d[2];  // OK: index 2 < size 3
}

void test_list_assign_init_list() {
    std::list<int> lst;
    lst.assign({1, 2});  // _size = 2
    lst.front();  // OK: size >= 1
}

void test_vector_constructor_init_list() {
    std::vector<int> v = {1, 2, 3};  // _size = 3
    v[2];  // OK: index 2 < size 3
}

void test_assign_after_operations() {
    std::vector<int> v;
    v.push_back(99);  // _size = 1
    v.assign({1, 2, 3, 4});  // _size = 4 (replaces)
    v[3];  // OK: index 3 < size 4
}

int main() { return 0; }
