#include <vector>

void example(unsigned i, const std::vector<int>& limits) {
    std::vector<int> values;
#if CASE == 0
    if (i < limits.size()) values.push_back(1);
#elif CASE == 1
    if (i < limits.size()) {} else values.push_back(1);
#elif CASE == 2
    if (i < limits.size()) values.push_back(1); else values.clear();
#elif CASE == 3
    if (i < limits.size()) values.push_back(1); else values.push_back(2);
#elif CASE == 4
    if (i < limits.size()) return; else values.push_back(1);
#elif CASE == 5
    if (i < limits.size()) values.push_back(1); else return;
#elif CASE == 6
    if (i < limits.size()) { values.push_back(1); return; }
#elif CASE == 7
    if (i >= limits.size()) return;
    (void)limits[i]; // The surviving branch keeps its bound proof.
    values.push_back(1);
#elif CASE == 8
    if (i < limits.size()) {} else return;
    (void)limits[i]; // Else-return also keeps the surviving proof.
    values.push_back(1);
#endif
    (void)values.front();
}
