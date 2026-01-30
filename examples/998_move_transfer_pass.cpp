// Example 998: Move semantics transfers scope-exit constraints
// When ownership is transferred via std::move, the constraint moves too
//
// This tests that ~requires constraints follow move semantics properly.

struct FILE_handle {};

[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* my_fopen(const char* path, const char* mode);

[[clang::annotate("dtt:requires=arg0.@open == 1")]]
[[clang::annotate("dtt:ensures=arg0.@open == 0")]]
int my_fclose(FILE_handle* f);

// Variant 1: Move to new variable, close via new variable
void variant1_move_and_close() {
    FILE_handle* f = my_fopen("data.txt", "r");
    FILE_handle* g = static_cast<FILE_handle*>(static_cast<void*>(f));  // Transfer
    f = nullptr;  // Original cleared
    my_fclose(g);  // Close via new owner
}  // OK: g was closed, f has no constraint (transferred)

// Variant 2: Simple reassignment transfer
void variant2_reassign() {
    FILE_handle* f = my_fopen("data.txt", "r");
    FILE_handle* g = f;  // Copy pointer (both point to same resource)
    f = nullptr;  // Clear original
    my_fclose(g);  // Close via copy
}  // OK: resource closed

// Variant 3: std::move (if available)
namespace std {
    template<typename T> T&& move(T& x) { return static_cast<T&&>(x); }
}

void variant3_std_move() {
    FILE_handle* f = my_fopen("data.txt", "r");
    FILE_handle* g = std::move(f);
    f = nullptr;
    my_fclose(g);
}  // OK: ownership transferred via move

int main() {
    variant1_move_and_close();
    variant2_reassign();
    variant3_std_move();
    return 0;
}
