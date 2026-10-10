#include <vector>
#include <cstdio>

void test() {
#if CASE == 0
    std::vector<int> values;
    values.at(0);
#elif CASE == 1
    std::vector<int> values;
    values.push_back(1);
    values.at(0);
#elif CASE == 2
    fgetc(nullptr);
#elif CASE == 3
    fflush(nullptr);
#endif
}
