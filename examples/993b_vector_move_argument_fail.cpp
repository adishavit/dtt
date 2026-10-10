// Moving into a by-value argument invalidates the source's tracked size.
#include <utility>
#include <vector>
void consume(std::vector<int>);
void count_constructor() {
    std::vector<int> v(3);
    consume(std::move(v));
    (void)v[0]; // dtt-error {{_size}}
}
void list_constructor() {
    std::vector<int> v{1, 2, 3};
    consume(std::move(v));
    (void)v[0]; // dtt-error {{_size}}
}
