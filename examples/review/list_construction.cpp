#include <vector>
#include <utility>
#if CASE == 9
// This user's initializer-list constructor deliberately does not mean size.
struct vector {
    [[clang::annotate("dtt:ensures=_size=0")]] vector(std::initializer_list<int>);
    [[clang::annotate("dtt:requires=_size > 0")]] void front();
};
#endif
void test() {
#if CASE == 0
    std::vector<int> v = {1, 2, 3}; (void)v[2];
#elif CASE == 1
    std::vector<int> v{3}; (void)v[0];
#elif CASE == 2
    std::vector<int> v{3}; (void)v[1];
#elif CASE == 3
    std::vector<int> v({1, 2, 3}, std::allocator<int>{}); (void)v[3];
#elif CASE == 4
    std::vector<int> v(std::initializer_list<int>{}); (void)v.front();
#elif CASE == 5
    std::vector<int> v(3); (void)v[2];
#elif CASE == 6
    std::vector<int> v{}; (void)v.front();
#elif CASE == 7
    std::vector<int> a{1, 2, 3}; std::vector<int> v(a); (void)v[2];
#elif CASE == 8
    std::vector<int> a{1, 2, 3}; std::vector<int> v(std::move(a)); (void)v[2];
#elif CASE == 9
    vector v{1, 2, 3}; v.front();
#endif
}
