// 1738: Inline pointer offset in a mem* argument is not tracked - DTT gap
//
// `char* p = data + 3; memcpy(out, p, n);` is checked against data's extent
// minus 3. Writing the offset inline, `memcpy(out, data + 3, n)`, gives an
// argument with no tracked path, so the source check is skipped entirely.
// Rename to _fail (and add dtt-error markers) when DTT reports these.

#include <cstddef>
#include <cstdlib>
#include <cstring>

// Variant 1: unguarded copy from an inline offset
void variant1_unguarded(char *out, size_t len, size_t n) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    memcpy(out, data + 3, n);  // BUG: n may exceed len - 3
    free(data);
}

// Variant 2: guard misses the offset
void variant2_offset_ignored(char *out, size_t len, size_t n) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    if (n > len) { free(data); return; }
    memcpy(out, data + 3, n);  // BUG: n may exceed len - 3
    free(data);
}

// Variant 3: constant-size array, inline offset
void variant3_array(char *out) {
    char buf[8] = {};
    memcpy(out, buf + 4, 8);  // BUG: only 4 bytes remain
}

int main() { return 0; }
