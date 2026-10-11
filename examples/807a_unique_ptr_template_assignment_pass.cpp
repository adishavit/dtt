#include <memory>
#include <optional>

void assign_nonempty_scalar() {
    std::unique_ptr<int> destination;
    auto source = std::make_unique<int>(1);
    destination = std::move(source);
    (void)*destination;
}

struct Value { int member; };

void assign_nonempty_record() {
    std::unique_ptr<Value> destination;
    auto source = std::make_unique<Value>();
    destination = std::move(source);
    (void)destination->member;
}

void optional_value_assignment() {
    std::optional<int> destination;
    destination = 7;
    (void)*destination;
}
