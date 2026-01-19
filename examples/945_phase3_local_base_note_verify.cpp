// 945_phase3_local_base_note_verify.cpp
#include <cstddef>
#include <cstring>

void inner_local(size_t off, size_t n) {
    char local[16];
    char* p = local + off;
    memset(p, 0, n);  // expected-warning {{cannot verify memset size fits in symbolic-sized buffer}}
                      // expected-note {{add bounds check: if (copy_size <= buffer_size)}}
                      // expected-note {{inferred precondition: local._size_bytes >= n+(off)}}
}

int main() { return 0; }
