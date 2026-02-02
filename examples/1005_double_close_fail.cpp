// Example 1005: Double close after copy - use after close
// When two pointers alias the same resource, closing via one
// should invalidate the other.

struct FILE_handle {};

[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* my_fopen(const char* path, const char* mode);

[[clang::annotate("dtt:requires=arg0.@open == 1")]]
[[clang::annotate("dtt:ensures=arg0.@open == 0")]]
int my_fclose(FILE_handle* f);

// Double close via alias - ERROR
void double_close_alias() {
    FILE_handle* f = my_fopen("data.txt", "r");
    FILE_handle* g = f;  // g aliases f
    my_fclose(f);        // OK: f.@open == 1, after: f.@open = 0
    my_fclose(g);        // ERROR: g.@open should be 0 (aliased f)
}

// Close original after transfer - ERROR (nullptr)
void close_after_transfer() {
    FILE_handle* f = my_fopen("data.txt", "r");
    FILE_handle* g = f;  // transfer
    f = nullptr;         // explicit release
    my_fclose(g);        // OK: close via g
    // my_fclose(f);     // Would error: f is nullptr
}

int main() {
    return 0;
}
