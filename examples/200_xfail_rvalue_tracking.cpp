// XFAIL: Rvalue reference tracking — should COMPILE but currently FAILS
//
// WHAT IS TESTED:
// When an object is moved via std::move(), the tracked properties should
// transfer to the destination and the source should be invalidated.
//
// WHY IT'S DIFFICULT:
// 1. Need to detect std::move() calls and understand move semantics
// 2. Must track that properties transfer from source to destination
// 3. Source object enters "moved-from" state - accessing it should warn
// 4. Compiler doesn't distinguish moved-from objects at type level
//
// RATIONALE:
// Move semantics are pervasive in modern C++. Without tracking moves:
// - Properties are lost when objects are moved into containers
// - Can't verify constraints on moved-to objects
// - No warning when accessing moved-from objects
//
// CURRENT BEHAVIOR:
// DTT treats std::move() as a regular function call. Properties on the
// source are invalidated (good), but not transferred to destination (bad).

#include <utility>

struct Buffer {
    int* data;
    int _size;
};

[[clang::annotate("dtt:requires=_size > 0")]]
void process(Buffer& b);

int main() {
    Buffer a;
    a._size = 10;  // a._size is known to be 10

    Buffer b = std::move(a);  // Should transfer: b._size = 10

    // XFAIL: b._size is unknown after move (properties not transferred)
    process(b);  // Should PASS: b._size was 10, now b owns it

    return 0;
}
