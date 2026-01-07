// Example 143: new (std::nothrow) returns unknown nullability
// Tests that DTT correctly handles nothrow new which can return nullptr

#include <new>

struct Data {
    int value;
};

int main() {
    // Regular new - guaranteed non-null (throws on failure)
    Data* p1 = new Data();
    p1->value = 1;  // OK - new always returns non-null

    // nothrow new - can return nullptr on allocation failure
    Data* p2 = new (std::nothrow) Data();
    p2->value = 2;  // ERROR: p2 could be null

    return 0;
}
