// MSVC STL implements unique_ptr's same-type move assignment as a function
// template. It must preserve the source's null state just like a special member.
#include <memory>

void assign_empty_scalar() {
    auto destination = std::make_unique<int>(1);
    std::unique_ptr<int> source;
    destination = std::move(source);
    (void)*destination; // dtt-error {{_nonnull}}
}

struct Value { int member; };

void assign_empty_record() {
    auto destination = std::make_unique<Value>();
    std::unique_ptr<Value> source;
    destination = std::move(source);
    (void)destination->member; // dtt-error {{_nonnull}}
}
