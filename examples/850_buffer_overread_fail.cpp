// 850_buffer_overread_fail.cpp
// Tests: Buffer over-read vulnerabilities (DTT catches all CWE-126 patterns)
// Information disclosure via reading beyond buffer bounds

#include <cstring>
#include <cstdint>
#include <cstdio>

// =============================================================================
// SECTION 1: memcpy Over-Read (Source Too Small)
// =============================================================================

// Variant 1.1: Copy more than source contains
void overread_memcpy_simple() {
    char src[10] = "hello";
    char dst[100];
    memcpy(dst, src, 50);  // ERROR: reading 50 bytes from 10-byte buffer
}

// Variant 1.2: User controls copy length
void overread_memcpy_user_len(const char* src, size_t src_size,
                              size_t user_len, char* dst) {
    memcpy(dst, src, user_len);  // ERROR: user_len not validated against src_size
}

// Variant 1.3: Copy from struct member beyond its size
void overread_struct_member() {
    struct Data {
        char name[16];
        int value;
    } d = {"test", 42};

    char buf[100];
    memcpy(buf, d.name, 32);  // ERROR: reading 32 from 16-byte member
}

// =============================================================================
// SECTION 2: memmove Over-Read
// =============================================================================

// Variant 2.1: memmove with wrong size
void overread_memmove() {
    uint8_t small[8];
    uint8_t large[64];
    memmove(large, small, 32);  // ERROR: reading 32 from 8-byte buffer
}

// =============================================================================
// SECTION 3: memcmp Over-Read
// =============================================================================

// Variant 3.1: Compare beyond buffer bounds
int overread_memcmp() {
    char a[5] = "test";
    char b[100] = "testing123...";
    return memcmp(a, b, 20);  // ERROR: reading 20 from 5-byte buffer 'a'
}

// Variant 3.2: Compare with parameter length
int overread_memcmp_param(const void* a, size_t a_len,
                          const void* b, size_t b_len,
                          size_t cmp_len) {
    return memcmp(a, b, cmp_len);  // ERROR: cmp_len might exceed a_len or b_len
}

// Variant 3.3: HMAC compare over-read
int vulnerable_hmac_compare(const uint8_t* expected, size_t exp_len,
                            const uint8_t* received, size_t recv_len,
                            size_t compare_len) {
    // compare_len from attacker, could be > exp_len
    return memcmp(expected, received, compare_len);  // ERROR: over-read
}

// =============================================================================
// SECTION 4: memchr Over-Read
// =============================================================================

// Variant 4.1: Search beyond buffer
const void* overread_memchr() {
    char buf[10] = "hello";
    return memchr(buf, 'x', 100);  // ERROR: searching 100 bytes in 10-byte buffer
}

// Variant 4.2: Search with untrusted length
const void* overread_memchr_param(const void* buf, size_t buf_size,
                                  int c, size_t search_len) {
    return memchr(buf, c, search_len);  // ERROR: search_len might > buf_size
}

// =============================================================================
// SECTION 5: String Over-Read via Untrusted Length
// =============================================================================

// Variant 5.1: strncpy with wrong source length assumption
void overread_strncpy(char* dst, size_t dst_size) {
    char src[10] = "hi";
    strncpy(dst, src, 50);  // ERROR: reads up to 50 from 10-byte buffer
}

// Variant 5.2: snprintf reading beyond source
void overread_snprintf() {
    char small[5] = "test";
    char big[100];
    snprintf(big, sizeof(big), "%.*s", 50, small);  // ERROR: precision > buffer
}

// =============================================================================
// SECTION 6: Array Index Over-Read
// =============================================================================

// Variant 6.1: Read with unchecked index
char overread_index(const char* arr, size_t arr_size, size_t index) {
    return arr[index];  // ERROR: index might >= arr_size
}

// Variant 6.2: Read at end of array
char overread_end() {
    char buf[10];
    return buf[10];  // ERROR: index 10 in size-10 array
}

// Variant 6.3: Negative-wraparound index
char overread_wraparound(const char* buf, size_t size, int index) {
    // If index is negative and cast to size_t, becomes huge
    return buf[static_cast<size_t>(index)];  // ERROR: negative index wraps
}

// =============================================================================
// SECTION 7: Pointer Arithmetic Over-Read
// =============================================================================

// Variant 7.1: Read via advanced pointer
char overread_ptr_arithmetic() {
    char buf[10];
    char* p = buf + 5;
    return p[10];  // ERROR: p + 10 = buf + 15, but buf only has 10 bytes
}

// Variant 7.2: Iterator past end
void overread_iterator_past_end() {
    int arr[5] = {1, 2, 3, 4, 5};
    int* end = arr + 5;
    int val = *end;  // ERROR: dereferencing one-past-end
    (void)val;
}

// =============================================================================
// SECTION 8: Struct/Union Over-Read
// =============================================================================

// Variant 8.1: Read larger type from smaller buffer
void overread_type_punning() {
    char buf[2];
    uint32_t val = *reinterpret_cast<uint32_t*>(buf);  // ERROR: 4-byte read from 2-byte buf
    (void)val;
}

// Variant 8.2: Read past flexible array member
struct FlexArray {
    uint32_t count;
    uint8_t data[1];  // Flexible array
};

void overread_flex_array(const FlexArray* fa, size_t total_size) {
    // Trust fa->count without checking against total_size
    for (uint32_t i = 0; i < fa->count; i++) {
        uint8_t b = fa->data[i];  // ERROR: fa->count might > actual data
        (void)b;
    }
}

// =============================================================================
// SECTION 9: Loop Over-Read
// =============================================================================

// Variant 9.1: Loop bound from untrusted source
void overread_loop(const uint8_t* data, size_t actual_len, uint32_t claimed_len) {
    uint8_t sum = 0;
    for (uint32_t i = 0; i < claimed_len; i++) {
        sum += data[i];  // ERROR: claimed_len might > actual_len
    }
}

// Variant 9.2: Off-by-one loop
void overread_loop_obo() {
    char buf[10];
    for (int i = 0; i <= 10; i++) {  // ERROR: should be i < 10
        char c = buf[i];
        (void)c;
    }
}

// =============================================================================
// SECTION 10: Protocol Field Over-Read
// =============================================================================

struct ProtocolMessage {
    uint16_t msg_type;
    uint16_t field_count;
    uint32_t fields[1];  // Variable length
};

// Variant 10.1: Trust field_count
void overread_protocol_fields(const ProtocolMessage* msg, size_t msg_size) {
    for (uint16_t i = 0; i < msg->field_count; i++) {
        uint32_t field = msg->fields[i];  // ERROR: field_count unvalidated
        (void)field;
    }
}

// Variant 10.2: Variable-length field read
void overread_varlen_field(const uint8_t* data, size_t data_len) {
    uint8_t tag = data[0];
    uint16_t length = (data[1] << 8) | data[2];
    const uint8_t* value = data + 3;

    uint8_t local[256];
    for (uint16_t i = 0; i < length; i++) {
        local[i] = value[i];  // ERROR: length not checked against data_len
    }
    (void)tag;
}

// =============================================================================
// SECTION 11: File Format Over-Read
// =============================================================================

struct ImageHeader {
    uint32_t magic;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_data_offset;
    uint32_t pixel_data_size;
};

// Variant 11.1: Trust header's size fields
void overread_image_data(const uint8_t* file_data, size_t file_size) {
    const ImageHeader* hdr = reinterpret_cast<const ImageHeader*>(file_data);

    uint8_t pixels[65536];
    // Trust hdr->pixel_data_size without checking against file_size
    memcpy(pixels, file_data + hdr->pixel_data_offset, hdr->pixel_data_size);  // ERROR
}

// Variant 11.2: Chunk-based format
struct Chunk {
    uint32_t type;
    uint32_t size;
};

void overread_chunk(const uint8_t* data, size_t remaining) {
    const Chunk* chunk = reinterpret_cast<const Chunk*>(data);
    uint8_t content[1024];

    if (chunk->size <= sizeof(content)) {
        // Only check dest size, not source!
        memcpy(content, data + 8, chunk->size);  // ERROR: chunk->size > remaining possible
    }
}

// =============================================================================
// SECTION 12: Crypto Over-Read
// =============================================================================

// Variant 12.1: Read key material beyond buffer
void overread_key_material(const uint8_t* key_blob, size_t blob_size,
                           uint8_t key_len, uint8_t* key_out) {
    // Trust key_len from header
    memcpy(key_out, key_blob + 4, key_len);  // ERROR: key_len might > blob_size - 4
}

// Variant 12.2: IV/nonce over-read
void overread_iv(const uint8_t* header, size_t header_size,
                 uint8_t* iv, uint8_t iv_len) {
    // iv_len specified by protocol but not validated
    memcpy(iv, header + 2, iv_len);  // ERROR: iv_len might > header_size - 2
}

// =============================================================================
// SECTION 13: Stack Over-Read (via pointer)
// =============================================================================

// Variant 13.1: Return pointer to local and read
const char* overread_stack_return() {
    char local[10] = "hello";
    return local;  // ERROR: returning pointer to local
    // Caller might read from dangling pointer
}

void overread_caller() {
    const char* p = overread_stack_return();
    char c = p[0];  // ERROR: reading from freed stack frame
    (void)c;
}

// =============================================================================
// SECTION 14: Information Leak via Uninitialized Read
// =============================================================================

// Variant 14.1: Send uninitialized stack data
void overread_uninitialized(int socket) {
    char buf[100];
    buf[0] = 'A';
    buf[1] = 'B';
    // Rest of buf is uninitialized - contains old stack data!
    // send(socket, buf, 100, 0);  // Would leak 98 bytes of stack
    (void)socket;
    (void)buf;
}

// Variant 14.2: Struct padding leak
struct PaddedStruct {
    uint8_t a;
    // 3 bytes padding
    uint32_t b;
    uint16_t c;
    // 2 bytes padding
};

void overread_padding(int socket) {
    PaddedStruct s;
    s.a = 1;
    s.b = 2;
    s.c = 3;
    // Padding bytes are uninitialized!
    // send(socket, &s, sizeof(s), 0);  // Would leak padding bytes
    (void)socket;
}

// =============================================================================
// SECTION 15: Cross-Buffer Over-Read
// =============================================================================

// Variant 15.1: Read from adjacent buffer
void overread_adjacent() {
    struct {
        char secret[16];
        char public_data[16];
    } buffers;

    strcpy(buffers.secret, "SECRET_KEY_123");
    strcpy(buffers.public_data, "public");

    char output[32];
    // Attacker requests 32 bytes from public_data
    memcpy(output, buffers.public_data, 32);  // ERROR: reads into secret!
}

int main() {
    return 0;
}
