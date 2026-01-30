// Example 998b: Move semantics - leak after transfer
// The constraint moves to the new owner, who must satisfy it

struct FILE_handle {};

[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* my_fopen(const char* path, const char* mode);

[[clang::annotate("dtt:requires=arg0.@open == 1")]]
[[clang::annotate("dtt:ensures=arg0.@open == 0")]]
int my_fclose(FILE_handle* f);

// Move ownership but new owner doesn't close - LEAK
void move_but_leak() {
    FILE_handle* f = my_fopen("data.txt", "r");
    FILE_handle* g = f;  // Transfer
    f = nullptr;         // Original cleared
    // g is not closed!
}  // ERROR: g.@open == 1, needs @open == 0

int main() {
    move_but_leak();
    return 0;
}
