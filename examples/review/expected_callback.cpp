#include "../../include/dtt_expected.h"

std::expected<int, int> fail(int) { return std::unexpected{1}; }
std::expected<int, int> unknown(int);
int twice(int x) { return x * 2; }

void example() {
#if CASE == 0
    std::expected<int, int> e(42);
    auto result = e.and_then(fail);
    (void)*result;
#elif CASE == 1
    std::expected<int, int> e(42);
    auto result = e.and_then(unknown);
    (void)*result;
#elif CASE == 2
    std::expected<int, int> e(std::unexpected{1});
    auto result = e.and_then(unknown);
    (void)*result;
#elif CASE == 3
    std::expected<int, int> e(42);
    auto result = e.and_then(unknown);
    if (result.has_value()) (void)*result;
#elif CASE == 4
    std::expected<int, int> e(42);
    auto result = e.transform(twice);
    (void)*result;
#endif
}
