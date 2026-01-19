// 848_reinterpret_cast_safe_pass.cpp
// Tests: Safe reinterpret_cast patterns with size validation
// DTT should NOT produce false positives when size is validated before cast
//
// These patterns use: if (len < sizeof(Struct)) return;
// After this guard, reinterpret_cast<Struct*>(buffer) is safe to dereference

#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <algorithm>

// =============================================================================
// SECTION 1: TLS Heartbeat - Safe Implementation
// =============================================================================

// The actual Heartbleed bug (CVE-2014-0160) was in OpenSSL's TLS heartbeat:
// - Client sends: type(1) + length(2) + payload(N) + padding(16)
// - Server echoes back: type(1) + length(2) + payload(N) + padding(16)
// - BUG: Server trusted client's "length" field without checking actual payload size

struct HeartbeatRequest {
    uint8_t type;
    uint16_t payload_length;  // Attacker-controlled!
    uint8_t payload[1];       // Flexible array member pattern
};

// Variant 1.1: Validate payload_length against actual received bytes
void safe_heartbeat_handler(const uint8_t* record, size_t record_len) {
    if (record_len < 3) return;  // Need at least type + length

    uint16_t payload_length = (record[1] << 8) | record[2];
    size_t actual_payload_size = record_len - 3;

    // SAFE: Validate claimed length against actual data
    if (payload_length > actual_payload_size) {
        return;  // Reject: client claims more than they sent
    }

    uint8_t response[1024];
    if (payload_length <= sizeof(response) - 3) {
        response[0] = record[0];  // type
        response[1] = record[1];  // length high
        response[2] = record[2];  // length low
        memcpy(response + 3, record + 3, payload_length);  // OK: validated
    }
}

// Variant 1.2: Use min() to clamp to actual size
void safe_heartbeat_min(const uint8_t* data, size_t data_len,
                        uint16_t claimed_len, uint8_t* response) {
    size_t safe_len = std::min(static_cast<size_t>(claimed_len), data_len);
    if (safe_len <= 1024) {
        memcpy(response, data, safe_len);  // OK: clamped to actual
    }
}

// =============================================================================
// SECTION 2: Network Protocol Parsing - Safe Patterns
// =============================================================================

struct PacketHeader {
    uint32_t magic;
    uint32_t payload_size;  // Attacker-controlled
};

// Variant 2.1: Validate header.payload_size against buffer
void safe_parse_packet(const uint8_t* buffer, size_t buffer_len) {
    if (buffer_len < sizeof(PacketHeader)) return;

    const PacketHeader* hdr = reinterpret_cast<const PacketHeader*>(buffer);
    size_t available = buffer_len - sizeof(PacketHeader);

    // SAFE: Check claimed size against actual available data
    if (hdr->payload_size > available) {
        return;  // Reject malformed packet
    }

    uint8_t local[256];
    if (hdr->payload_size <= sizeof(local)) {
        memcpy(local, buffer + sizeof(PacketHeader), hdr->payload_size);  // OK
    }
}

// Variant 2.2: Extract and validate before use
void safe_extract_field(const uint8_t* msg, size_t msg_len,
                        size_t offset, size_t field_len, uint8_t* out) {
    // SAFE: Bounds check before memcpy
    if (offset > msg_len) return;
    if (field_len > msg_len - offset) return;  // Prevent overflow
    if (field_len > 256) return;  // Output buffer limit

    memcpy(out, msg + offset, field_len);  // OK: all bounds checked
}

// =============================================================================
// SECTION 3: Echo/Reflection Services - Safe Patterns
// =============================================================================

// Variant 3.1: Echo with explicit length validation
void safe_echo(const char* input, size_t input_len,
               size_t requested_echo_len, char* output, size_t output_size) {
    // SAFE: Validate against BOTH source and destination
    if (requested_echo_len > input_len) return;
    if (requested_echo_len > output_size) return;

    memcpy(output, input, requested_echo_len);  // OK: double validated
}

// Variant 3.2: Echo with clamping
size_t safe_echo_clamped(const char* src, size_t src_len,
                         size_t requested, char* dst, size_t dst_len) {
    size_t actual = requested;
    if (actual > src_len) actual = src_len;
    if (actual > dst_len) actual = dst_len;

    memcpy(dst, src, actual);  // OK: clamped to min of all constraints
    return actual;
}

// =============================================================================
// SECTION 4: String/TLV Parsing - Safe Patterns
// =============================================================================

struct TLV {  // Type-Length-Value
    uint8_t type;
    uint16_t length;  // Attacker-controlled
    uint8_t value[1];
};

// Variant 4.1: Validate TLV length against container
void safe_parse_tlv(const uint8_t* container, size_t container_len) {
    size_t offset = 0;

    while (offset + 3 <= container_len) {  // Need at least T+L
        uint8_t type = container[offset];
        uint16_t length = (container[offset+1] << 8) | container[offset+2];

        // SAFE: Validate value fits in remaining container
        if (offset + 3 + length > container_len) {
            break;  // Malformed TLV
        }

        uint8_t value_buf[1024];
        if (length <= sizeof(value_buf)) {
            memcpy(value_buf, container + offset + 3, length);  // OK
        }

        offset += 3 + length;
        (void)type;
    }
}

// Variant 4.2: Length-prefixed string with validation
void safe_read_length_prefixed(const uint8_t* data, size_t data_len,
                               char* out, size_t out_len) {
    if (data_len < 2) return;

    uint16_t str_len = (data[0] << 8) | data[1];

    // SAFE: Validate against source AND destination
    if (str_len > data_len - 2) return;
    if (str_len >= out_len) return;  // Need room for null

    memcpy(out, data + 2, str_len);  // OK
    out[str_len] = '\0';
}

// =============================================================================
// SECTION 5: Chunked Transfer - Safe Patterns
// =============================================================================

// Variant 5.1: Process chunk with size validation
void safe_process_chunk(const uint8_t* chunk_data, size_t chunk_available,
                        uint32_t claimed_chunk_size, uint8_t* dest) {
    // SAFE: Validate claimed size
    if (claimed_chunk_size > chunk_available) return;
    if (claimed_chunk_size > 4096) return;  // Max chunk size

    memcpy(dest, chunk_data, claimed_chunk_size);  // OK
}

// =============================================================================
// SECTION 6: Buffer Copy with Explicit Bounds - Safe Patterns
// =============================================================================

// Variant 6.1: All three sizes validated
void safe_bounded_copy(const void* src, size_t src_size,
                       void* dst, size_t dst_size,
                       size_t requested_size) {
    if (requested_size > src_size) return;
    if (requested_size > dst_size) return;

    memcpy(dst, src, requested_size);  // OK: triple bounded
}

// Variant 6.2: Return actual copied amount
size_t safe_copy_available(const void* src, size_t src_avail,
                           void* dst, size_t dst_avail) {
    size_t to_copy = (src_avail < dst_avail) ? src_avail : dst_avail;
    memcpy(dst, src, to_copy);  // OK: uses minimum
    return to_copy;
}

// =============================================================================
// SECTION 7: Message Framing - Safe Patterns
// =============================================================================

struct MessageFrame {
    uint32_t total_length;  // Including header
    uint32_t payload_offset;
    uint32_t payload_length;
};

// Variant 7.1: Validate frame consistency
void safe_read_framed_message(const uint8_t* frame, size_t frame_len) {
    if (frame_len < sizeof(MessageFrame)) return;

    const MessageFrame* mf = reinterpret_cast<const MessageFrame*>(frame);

    // SAFE: Validate all offsets and lengths
    if (mf->total_length > frame_len) return;
    if (mf->payload_offset > mf->total_length) return;
    if (mf->payload_length > mf->total_length - mf->payload_offset) return;

    uint8_t payload[2048];
    if (mf->payload_length <= sizeof(payload)) {
        memcpy(payload, frame + mf->payload_offset, mf->payload_length);  // OK
    }
}

// =============================================================================
// SECTION 8: ASN.1/DER Style Length - Safe Patterns
// =============================================================================

// Variant 8.1: Parse DER length with validation
size_t safe_parse_der_length(const uint8_t* data, size_t data_len,
                             size_t* consumed) {
    if (data_len < 1) { *consumed = 0; return 0; }

    if (data[0] < 128) {
        *consumed = 1;
        return data[0];  // Short form
    }

    size_t num_octets = data[0] & 0x7F;
    if (num_octets > 4) { *consumed = 0; return 0; }  // Sanity limit
    if (1 + num_octets > data_len) { *consumed = 0; return 0; }

    size_t length = 0;
    for (size_t i = 0; i < num_octets; i++) {
        length = (length << 8) | data[1 + i];
    }

    *consumed = 1 + num_octets;
    return length;
}

void safe_read_der_content(const uint8_t* der, size_t der_len) {
    if (der_len < 2) return;

    size_t len_consumed;
    size_t content_len = safe_parse_der_length(der + 1, der_len - 1, &len_consumed);

    size_t header_len = 1 + len_consumed;

    // SAFE: Validate content fits
    if (header_len > der_len) return;
    if (content_len > der_len - header_len) return;

    uint8_t content[1024];
    if (content_len <= sizeof(content)) {
        memcpy(content, der + header_len, content_len);  // OK
    }
}

// =============================================================================
// SECTION 9: Memcmp with Validated Length - Safe Patterns
// =============================================================================

// Variant 9.1: Compare with length validation
int safe_memcmp_bounded(const void* a, size_t a_len,
                        const void* b, size_t b_len,
                        size_t cmp_len) {
    if (cmp_len > a_len) return -2;  // Error: a too short
    if (cmp_len > b_len) return -2;  // Error: b too short

    return memcmp(a, b, cmp_len);  // OK: validated
}

// =============================================================================
// SECTION 10: Response Construction - Safe Patterns
// =============================================================================

// Variant 10.1: Build response with proper sizing
void safe_build_response(const uint8_t* request, size_t request_len,
                         uint16_t echo_len, uint8_t* response, size_t response_size) {
    const size_t HEADER_SIZE = 4;

    // SAFE: Validate echo_len against source
    if (echo_len > request_len) return;

    // SAFE: Validate total response fits
    if (HEADER_SIZE + echo_len > response_size) return;

    // Build response
    response[0] = 0x02;  // Response type
    response[1] = (echo_len >> 8) & 0xFF;
    response[2] = echo_len & 0xFF;
    response[3] = 0x00;  // Padding
    memcpy(response + HEADER_SIZE, request, echo_len);  // OK
}

int main() {
    return 0;
}
