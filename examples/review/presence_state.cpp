#include <memory>
#include <optional>
#include <utility>
#include <any>

void example() {
#if CASE == 0
    std::unique_ptr<int> p(new int(1)); p.reset(nullptr); (void)*p;
#elif CASE == 1
    std::unique_ptr<int> p(new int(1)); p.reset(); (void)*p;
#elif CASE == 2
    std::unique_ptr<int> p; int* raw = nullptr; p.reset(raw); (void)*p;
#elif CASE == 3
    std::unique_ptr<int> p; p.reset(new int(1)); (void)*p;
#elif CASE == 4
    std::shared_ptr<int> p(new int(1)); p.reset(static_cast<int*>(nullptr)); (void)*p;
#elif CASE == 5
    std::shared_ptr<int> p; p.reset(new int(1)); (void)*p;
#elif CASE == 6
    std::shared_ptr<int> empty, p(new int(1)); p = empty; (void)*p;
#elif CASE == 7
    std::shared_ptr<int> empty, p(new int(1)); p = std::move(empty); (void)*p;
#elif CASE == 8
    std::optional<int> empty, p(1); p = empty; (void)*p;
#elif CASE == 9
    std::optional<int> empty, p(1); p = std::move(empty); (void)*p;
#elif CASE == 10
    std::optional<int> p; p = 42; (void)*p;
#elif CASE == 11
    std::shared_ptr<int> full(new int(1)), p; p = full; (void)*p;
#elif CASE == 12
    std::unique_ptr<int> p(new int(1)); p = nullptr; (void)*p;
#elif CASE == 13
    std::optional<int> p(1); p = std::nullopt; (void)*p;
#elif CASE == 14
    std::unique_ptr<int> empty, p(new int(1)); p = std::move(empty); (void)*p;
#elif CASE == 15
    std::optional<int> empty; empty = empty; (void)*empty;
#elif CASE == 16
    std::any source(42), destination(2.0); destination = source;
    (void)std::any_cast<int>(destination);
#elif CASE == 17
    std::any empty, destination(42); destination = empty;
    (void)std::any_cast<int>(destination);
#elif CASE == 18
    std::optional<int> p(1); p = std::optional<int>(); (void)*p;
#elif CASE == 19
    std::optional<int> p; p = std::optional<int>(42); (void)*p;
#elif CASE == 20
    std::unique_ptr<int> p(new int(1)); p = std::unique_ptr<int>(); (void)*p;
#elif CASE == 21
    std::unique_ptr<int> p; p = std::make_unique<int>(42); (void)*p;
#endif
}
