// 871_reinterpret_cast_null_pass.cpp
// Tests that reinterpret_cast preserves null tracking from source pointer
//
// reinterpret_cast only changes the type, not the value.
// If source is non-null, result is non-null.

#include <cstdint>
#include <cstring>

struct Header {
    uint32_t magic;
    uint32_t size;
};

// Variant 1: reinterpret_cast from guarded pointer
void test_reinterpret_cast_after_null_check(const uint8_t* buffer) {
    if (buffer == nullptr) return;

    const Header* hdr = reinterpret_cast<const Header*>(buffer);
    uint32_t x = hdr->magic;  // OK: buffer was null-checked, hdr is non-null
    (void)x;
}

// Variant 2: reinterpret_cast in chain
void test_reinterpret_cast_chain(const void* ptr) {
    if (!ptr) return;

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(ptr);
    const Header* hdr = reinterpret_cast<const Header*>(bytes);
    uint32_t x = hdr->size;  // OK: ptr was null-checked
    (void)x;
}

// Variant 3: reinterpret_cast with size check (heartbleed pattern)
void test_reinterpret_cast_with_size_check(const uint8_t* buffer, size_t len) {
    if (len < sizeof(Header)) return;  // Implies buffer is valid

    const Header* hdr = reinterpret_cast<const Header*>(buffer);
    uint32_t x = hdr->magic;  // OK: size check implies buffer is valid
    (void)x;
}

// Variant 4: explicit null check before cast
void test_explicit_null_before_cast(const uint8_t* data) {
    if (data != nullptr) {
        const Header* h = reinterpret_cast<const Header*>(data);
        uint32_t m = h->magic;  // OK: data is non-null
        (void)m;
    }
}

int main() { return 0; }
