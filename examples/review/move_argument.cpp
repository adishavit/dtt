// Actual construction, rather than an std::move cast, changes source tracking.
// Cases 0, 1, 6, 7, 8, 9 lose _size; cases 2-5 and 10-13 preserve it.
#include <typeinfo>
#include <utility>
#include <vector>

void consume(std::vector<int>);
void observe(const std::vector<int> &) {}
void bind(std::vector<int> &&) {}
[[clang::annotate("dtt:requires=arg0._size > 0")]]
void annotated_consume(std::vector<int>);
[[clang::annotate("dtt:ensures=arg0._size == 3")]]
void restore(std::vector<int> &, std::vector<int>);
struct Sink {
    void consume(std::vector<int>);
    void operator()(std::vector<int>);
};

void test() {
#if CASE == 4
    const std::vector<int> v(3);
#else
    std::vector<int> v(3);
#endif
#if CASE == 0
    consume(std::move(v));
#elif CASE == 1
    consume(static_cast<std::vector<int> &&>(v));
#elif CASE == 2
    (void)std::move(v);
#elif CASE == 3
    consume(v);
#elif CASE == 4
    consume(std::move(v)); // const rvalue selects the copy constructor.
#elif CASE == 5
    observe(std::move(v)); // Binding a const reference does not construct.
#elif CASE == 6
    annotated_consume(std::move(v));
#elif CASE == 7
    Sink s;
    s.consume(std::move(v));
#elif CASE == 8
    Sink s;
    s(std::move(v));
#elif CASE == 9
    void (*indirect)(std::vector<int>) = consume;
    indirect(std::move(v));
#elif CASE == 10
    bind(std::move(v)); // Visible empty body cannot modify the reference.
#elif CASE == 11
    (void)noexcept(consume(std::move(v)));
#elif CASE == 12
    (void)typeid(std::vector<int>(std::move(v)));
#elif CASE == 13
    restore(v, std::move(v)); // The callee restores v after argument construction.
#elif CASE == 14
    (void)requires { consume(std::move(v)); };
#elif CASE == 15
    using Result = decltype(consume(std::move(v)));
#endif
    (void)v[0];
}
