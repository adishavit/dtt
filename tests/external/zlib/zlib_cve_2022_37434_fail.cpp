// CVE-2022-37434: inflateGetHeader on uninitialized stream (should FAIL to compile)
//
// CVE-2022-37434 is a heap buffer overflow in zlib's inflateGetHeader that
// can be triggered when the function is called on a stream that has not been
// properly initialized with inflateInit2. DTT catches this because
// inflateGetHeader requires @stream_state == 2.
//
// Reference: https://nvd.nist.gov/vuln/detail/CVE-2022-37434

#include <zlib.h>
#include <cstring>

// Variant 1: inflateGetHeader on a never-initialized stream
void variant_get_header_no_init() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    gz_header head;
    memset(&head, 0, sizeof(head));

    // DTT ERROR: requires arg0.@stream_state == 2, but @stream_state == 0
    inflateGetHeader(&strm, &head);
}

// Variant 2: inflateGetHeader after inflateEnd — stream already torn down
void variant_get_header_after_end() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    gz_header head;
    memset(&head, 0, sizeof(head));

    inflateInit2(&strm, 15 + 16);     // @stream_state = 2 (gzip decoding)
    inflateEnd(&strm);                 // @stream_state = 0

    // DTT ERROR: requires arg0.@stream_state == 2, but @stream_state == 0
    inflateGetHeader(&strm, &head);
}

int main() {
    variant_get_header_no_init();
    variant_get_header_after_end();
    return 0;
}
