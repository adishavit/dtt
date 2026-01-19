// 867_initializer_list_size_fail.cpp
// Tests that DTT catches out-of-bounds with initializer_list

#include <vector>
#include <deque>

void test_vector_out_of_bounds() {
    std::vector<int> v;
    v.assign({1, 2, 3});  // _size = 3
    v[3];  // ERROR: index 3 >= size 3
}

void test_deque_out_of_bounds() {
    std::deque<int> d;
    d.assign({10, 20});  // _size = 2
    d[2];  // ERROR: index 2 >= size 2
}

int main() { return 0; }
