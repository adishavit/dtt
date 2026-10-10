// Known gap: the move constructor of a by-value argument does not invalidate
// the source's tracked size. A named destination's move construction does.
#include <utility>
#include <vector>
void consume(std::vector<int>);
void count_constructor() {
    std::vector<int> v(3);
    consume(std::move(v));
    (void)v[0];
}
void list_constructor() {
    std::vector<int> v{1, 2, 3};
    consume(std::move(v));
    (void)v[0];
}
