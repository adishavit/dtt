#include <vector>
#include <cstdlib>
#include <utility>
using std::vector;

void vulnerable(int* session) {
    vector<int> data(2);              // _size = 2
    data.pop_back();                  // _size = 1
    data.pop_back();                  // _size = 0
    int first = data.front();         // BUG 1: empty container

    if (rand() % 2)
        session = nullptr;
    *session = first;                 // BUG 2: null dereference

    vector<int> backup = std::move(data);
    data[0] = 999;                    // BUG 3: use after move
}
