// Example 996: Unified arg postcondition syntax - should pass (no DTT error)
// Tests that ensures=arg0.@field works for non-void functions
//
// This unifies the postcondition syntax:
// - ensures=result.@field  -> sets return value's field
// - ensures=arg0.@field    -> sets first argument's field (ANY function)
// - ensures=arg1.@field    -> sets second argument's field

struct Handle {};

// Acquisition: sets result.@open = 1
[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
Handle* acquire();

// Release: returns int status, but modifies arg0.@open
// This is the key test - arg0.@field on non-void function
[[clang::annotate("dtt:requires=arg0.@open == 1")]]
[[clang::annotate("dtt:ensures=arg0.@open == 0")]]
int release(Handle* h);  // Returns int, not void!

// Variant 1: Basic usage with int-returning release
void variant1_int_release() {
    Handle* h = acquire();
    int status = release(h);  // Should set h.@open = 0
    (void)status;
}  // OK: @open == 0 at scope exit

// Variant 2: Ignoring return value
void variant2_ignore_return() {
    Handle* h = acquire();
    release(h);  // Return value ignored, but h.@open still set to 0
}  // OK: @open == 0 at scope exit

// Variant 3: Check return value
void variant3_check_return() {
    Handle* h = acquire();
    if (release(h) != 0) {
        // Error handling - but h.@open is still 0
    }
}  // OK: @open == 0 at scope exit

// Variant 4: Multiple arguments - modify specific one
struct Buffer {};

[[clang::annotate("dtt:ensures=result.@valid == 1")]]
[[clang::annotate("dtt:~requires=result.@valid == 0")]]
Buffer* create_buffer();

// Copies from src to dst, marks dst as written
[[clang::annotate("dtt:ensures=arg0.@written == 1")]]
[[clang::annotate("dtt:ensures=arg0.@valid == 0")]]  // Consumes dst's ~requires
int copy_to(Buffer* dst, const char* src);

void variant4_multi_arg() {
    Buffer* b = create_buffer();
    copy_to(b, "hello");
}  // OK: @valid == 0 at scope exit

int main() {
    variant1_int_release();
    return 0;
}
