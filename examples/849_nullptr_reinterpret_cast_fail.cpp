// 849_nullptr_reinterpret_cast_fail.cpp
// Tests: Null pointer detection on reinterpret_cast patterns
//
// NOTE: DTT catches NULL POINTER issues here, NOT the actual buffer over-read
// vulnerability (Heartbleed CVE-2014-0160). True heartbleed detection would
// require tracking pointer/length associations and validating memcpy bounds.
//
// What DTT catches: reinterpret_cast<Struct*>(buffer)->field without null check
// What DTT does NOT catch: memcpy(out, data, attacker_controlled_length)

#include <cstring>
#include <cstdint>
#include <cstdlib>

// =============================================================================
// SECTION 1: The Exact Heartbleed Pattern (CVE-2014-0160)
// =============================================================================

// This is essentially what the vulnerable OpenSSL code did
struct HeartbeatMessage {
    uint8_t type;
    uint16_t payload_length;  // Attacker-controlled - THE VULNERABILITY
    // payload follows...
};

// Variant 1.1: The classic Heartbleed - trust payload_length blindly
void vulnerable_heartbeat_handler(const uint8_t* request, size_t request_len,
                                  uint8_t* response) {
    // Parse header (simplified)
    uint16_t payload_length = (request[1] << 8) | request[2];
    const uint8_t* payload = request + 3;

    // BUG: We trust payload_length without checking if that much data exists!
    // If attacker sends payload_length=65535 but only 1 byte of actual payload,
    // we read 65534 bytes of unrelated memory!

    response[0] = 0x02;  // Response type
    response[1] = request[1];
    response[2] = request[2];
    memcpy(response + 3, payload, payload_length);  // ERROR: payload_length unvalidated
}

// Variant 1.2: Heartbleed with explicit over-read
void heartbleed_overread(const uint8_t* data, size_t actual_data_len,
                         uint16_t claimed_length, uint8_t* out) {
    // Attacker claims data is 'claimed_length' bytes
    // But actual_data_len might be much smaller
    memcpy(out, data, claimed_length);  // ERROR: claimed_length not checked against actual_data_len
}

// Variant 1.3: Reading response from small request buffer
void heartbleed_echo(const char* request_payload, size_t request_size,
                     uint32_t echo_length, char* response) {
    // Client says "echo back echo_length bytes of my payload"
    // But what if echo_length > request_size?
    memcpy(response, request_payload, echo_length);  // ERROR: echo_length > request_size possible
}

// =============================================================================
// SECTION 2: Network Protocol Vulnerabilities
// =============================================================================

struct PacketHeader {
    uint32_t magic;
    uint32_t payload_size;  // From network - attacker controlled!
};

// Variant 2.1: Trust packet header's size field
void vulnerable_packet_handler(const uint8_t* packet, size_t packet_len) {
    const PacketHeader* hdr = reinterpret_cast<const PacketHeader*>(packet);
    const uint8_t* payload = packet + sizeof(PacketHeader);

    uint8_t local_buf[1024];
    // BUG: hdr->payload_size comes from attacker, not validated against packet_len
    memcpy(local_buf, payload, hdr->payload_size);  // ERROR: over-read if payload_size > available
}

// Variant 2.2: Length field from untrusted struct
void vulnerable_struct_length(const uint8_t* buffer, size_t buffer_len) {
    struct Message {
        uint16_t type;
        uint16_t length;  // Attacker-controlled
        uint8_t data[1];
    };

    const Message* msg = reinterpret_cast<const Message*>(buffer);
    uint8_t copy[512];

    // Trust msg->length without validation
    if (msg->length <= sizeof(copy)) {
        memcpy(copy, msg->data, msg->length);  // ERROR: msg->length not validated against buffer_len
    }
}

// Variant 2.3: Two length fields, only one checked
void vulnerable_partial_check(const uint8_t* data, size_t data_len,
                              uint16_t header_len, uint16_t body_len) {
    // Only check header_len against output buffer, not against source!
    uint8_t out[256];
    if (header_len <= 128) {
        memcpy(out, data, header_len);  // ERROR: header_len not checked against data_len
    }
    if (body_len <= 128) {
        memcpy(out + 128, data + header_len, body_len);  // ERROR: body_len not checked
    }
}

// =============================================================================
// SECTION 3: TLV (Type-Length-Value) Parser Vulnerabilities
// =============================================================================

// Variant 3.1: Trust TLV length field
void vulnerable_tlv_parse(const uint8_t* tlv_data, size_t data_len) {
    uint8_t type = tlv_data[0];
    uint16_t length = (tlv_data[1] << 8) | tlv_data[2];
    const uint8_t* value = tlv_data + 3;

    uint8_t extracted[1024];
    if (length <= sizeof(extracted)) {
        // BUG: length not validated against (data_len - 3)
        memcpy(extracted, value, length);  // ERROR: over-read possible
    }
    (void)type;
}

// Variant 3.2: Chained TLVs without cumulative bounds check
void vulnerable_tlv_chain(const uint8_t* container, size_t container_len) {
    size_t offset = 0;

    while (offset < container_len) {
        uint16_t length = (container[offset+1] << 8) | container[offset+2];

        uint8_t value[256];
        if (length <= sizeof(value)) {
            // BUG: No check that offset + 3 + length <= container_len
            memcpy(value, container + offset + 3, length);  // ERROR: over-read
        }
        offset += 3 + length;
    }
}

// =============================================================================
// SECTION 4: String Handling Vulnerabilities
// =============================================================================

// Variant 4.1: Length-prefixed string without validation
void vulnerable_length_prefixed_string(const uint8_t* data, size_t data_len) {
    uint16_t str_len = (data[0] << 8) | data[1];
    const char* str = reinterpret_cast<const char*>(data + 2);

    char buffer[256];
    if (str_len < sizeof(buffer)) {
        // BUG: str_len not checked against (data_len - 2)
        memcpy(buffer, str, str_len);  // ERROR: over-read if str_len > data_len - 2
        buffer[str_len] = '\0';
    }
}

// Variant 4.2: Pascal string copy
void vulnerable_pascal_string(const uint8_t* pstr, size_t max_len) {
    uint8_t length = pstr[0];  // First byte is length
    char dest[128];

    if (length < sizeof(dest)) {
        // Only check against dest, not against actual source data!
        memcpy(dest, pstr + 1, length);  // ERROR: length not checked against max_len
    }
}

// =============================================================================
// SECTION 5: Chunk/Fragment Processing Vulnerabilities
// =============================================================================

// Variant 5.1: Process chunk with unchecked size
void vulnerable_chunk_process(const uint8_t* chunk, size_t chunk_actual_size,
                              uint32_t chunk_claimed_size, uint8_t* dest) {
    // Trust the claimed size from the chunk header
    memcpy(dest, chunk, chunk_claimed_size);  // ERROR: chunk_claimed_size unvalidated
}

// Variant 5.2: Reassembly buffer overflow
void vulnerable_fragment_reassemble(const uint8_t* fragment, size_t frag_actual_len,
                                    uint16_t frag_offset, uint16_t frag_length,
                                    uint8_t* reassembly_buffer) {
    // Trust fragment metadata without validation
    memcpy(reassembly_buffer + frag_offset, fragment, frag_length);  // ERROR: frag_length unvalidated
}

// =============================================================================
// SECTION 6: Certificate/ASN.1 Parsing Vulnerabilities
// =============================================================================

// Variant 6.1: DER length field trusted blindly
void vulnerable_der_parse(const uint8_t* der, size_t der_len) {
    // Simplified DER: tag(1) + length(1-5) + content
    uint8_t tag = der[0];
    uint8_t length_byte = der[1];

    size_t content_length;
    const uint8_t* content;

    if (length_byte < 128) {
        content_length = length_byte;
        content = der + 2;
    } else {
        // Long form length - read N bytes
        size_t num_octets = length_byte & 0x7F;
        content_length = 0;
        for (size_t i = 0; i < num_octets; i++) {
            content_length = (content_length << 8) | der[2 + i];
        }
        content = der + 2 + num_octets;
    }

    uint8_t extracted[2048];
    if (content_length <= sizeof(extracted)) {
        // BUG: content_length from DER, not validated against der_len!
        memcpy(extracted, content, content_length);  // ERROR: over-read
    }
    (void)tag;
}

// Variant 6.2: X.509 extension length
void vulnerable_x509_extension(const uint8_t* ext, size_t ext_buffer_len,
                               uint16_t ext_claimed_len, uint8_t* out) {
    // Trust extension's self-reported length
    memcpy(out, ext, ext_claimed_len);  // ERROR: ext_claimed_len > ext_buffer_len possible
}

// =============================================================================
// SECTION 7: Database/Record Vulnerabilities
// =============================================================================

struct DatabaseRecord {
    uint32_t id;
    uint32_t data_length;  // Stored in DB - but could be corrupted/malicious
    uint8_t data[1];
};

// Variant 7.1: Trust stored record length
void vulnerable_read_record(const uint8_t* record_data, size_t available,
                            uint8_t* output) {
    const DatabaseRecord* rec = reinterpret_cast<const DatabaseRecord*>(record_data);

    // Trust rec->data_length from stored data
    memcpy(output, rec->data, rec->data_length);  // ERROR: data_length unvalidated
}

// Variant 7.2: Variable-length record
void vulnerable_varlen_record(const uint8_t* buffer, size_t buf_len) {
    uint32_t record_len = *reinterpret_cast<const uint32_t*>(buffer);
    uint8_t local[1024];

    if (record_len <= sizeof(local)) {
        // Only check output, not input!
        memcpy(local, buffer + 4, record_len);  // ERROR: record_len > buf_len-4 possible
    }
}

// =============================================================================
// SECTION 8: Compression/Decompression Vulnerabilities
// =============================================================================

// Variant 8.1: Decompressed size from header
void vulnerable_decompress(const uint8_t* compressed, size_t compressed_len,
                           uint32_t uncompressed_size, uint8_t* output) {
    // "Decompress" by just copying (simplified)
    // Trust uncompressed_size from compressed data header
    memcpy(output, compressed + 4, uncompressed_size);  // ERROR: reads beyond compressed data
}

// =============================================================================
// SECTION 9: Memory Comparison Vulnerabilities (timing-safe gone wrong)
// =============================================================================

// Variant 9.1: Compare with untrusted length
int vulnerable_memcmp(const void* expected, size_t expected_len,
                      const void* received, size_t received_len,
                      size_t compare_len) {
    // Trust compare_len without validation
    return memcmp(expected, received, compare_len);  // ERROR: may read beyond buffers
}

// Variant 9.2: HMAC verification with wrong length
void vulnerable_hmac_verify(const uint8_t* computed_mac, size_t computed_len,
                            const uint8_t* received_mac, size_t claimed_mac_len) {
    // Trust claimed_mac_len
    if (memcmp(computed_mac, received_mac, claimed_mac_len) == 0) {  // ERROR
        // Authenticated...
    }
}

// =============================================================================
// SECTION 10: Session/State Vulnerabilities
// =============================================================================

// Variant 10.1: Session data copy with size from session
struct Session {
    uint32_t session_id;
    uint32_t data_size;  // Could be corrupted
    uint8_t data[256];
};

void vulnerable_restore_session(const Session* session, uint8_t* restored_data) {
    // Trust session->data_size
    memcpy(restored_data, session->data, session->data_size);  // ERROR: may exceed 256
}

// =============================================================================
// SECTION 11: The "Echo Server" Pattern
// =============================================================================

// Variant 11.1: Echo with client-specified length
void vulnerable_echo_server(const char* client_data, size_t actual_len,
                            uint32_t client_requested_len, char* response) {
    // Client says "echo back N bytes" but only sent M bytes
    memcpy(response, client_data, client_requested_len);  // ERROR: Heartbleed!
}

// Variant 11.2: Ping with size
void vulnerable_ping(const uint8_t* ping_data, size_t ping_actual_size,
                     uint16_t ping_claimed_size, uint8_t* pong) {
    pong[0] = 0x01;  // Pong type
    memcpy(pong + 1, ping_data, ping_claimed_size);  // ERROR: over-read
}

// =============================================================================
// SECTION 12: Crypto Protocol Vulnerabilities
// =============================================================================

// Variant 12.1: Key exchange with length from peer
void vulnerable_key_exchange(const uint8_t* peer_key_data, size_t received_len,
                             uint16_t peer_claimed_key_len, uint8_t* our_copy) {
    // Peer claims their key is peer_claimed_key_len bytes
    memcpy(our_copy, peer_key_data, peer_claimed_key_len);  // ERROR: trust peer
}

// Variant 12.2: Nonce/IV copy with bad length
void vulnerable_nonce_copy(const uint8_t* header, size_t header_len,
                           uint8_t nonce_len, uint8_t* nonce_out) {
    // nonce_len from header - could claim more than header contains
    memcpy(nonce_out, header + 4, nonce_len);  // ERROR: nonce_len unvalidated
}

// =============================================================================
// SECTION 13: Serialization Vulnerabilities
// =============================================================================

// Variant 13.1: Deserialize with embedded length
void vulnerable_deserialize(const uint8_t* serialized, size_t buffer_len) {
    uint32_t string_len = *reinterpret_cast<const uint32_t*>(serialized);
    char string_buf[256];

    if (string_len < sizeof(string_buf)) {
        // Check output but not input
        memcpy(string_buf, serialized + 4, string_len);  // ERROR
    }
}

// Variant 13.2: Array deserialization
void vulnerable_deserialize_array(const uint8_t* data, size_t data_len) {
    uint16_t array_len = *reinterpret_cast<const uint16_t*>(data);
    uint16_t elem_size = *reinterpret_cast<const uint16_t*>(data + 2);

    uint8_t array[1024];
    size_t total = array_len * elem_size;  // Could also overflow!

    if (total <= sizeof(array)) {
        memcpy(array, data + 4, total);  // ERROR: total may exceed data_len - 4
    }
}

// =============================================================================
// SECTION 14: Off-by-N Variants of Heartbleed
// =============================================================================

// Variant 14.1: Almost correct - off by header size
void almost_correct_heartbleed(const uint8_t* request, size_t request_len) {
    uint16_t payload_len = (request[1] << 8) | request[2];

    // Check against total request length, forgot to subtract header!
    if (payload_len <= request_len) {  // BUG: should be (request_len - 3)
        uint8_t response[1024];
        memcpy(response, request + 3, payload_len);  // ERROR: off by 3
    }
}

// Variant 14.2: Wrong comparison operator
void wrong_comparison(const uint8_t* data, size_t data_len,
                      size_t requested_len, uint8_t* out) {
    // Using < instead of <=, or vice versa at wrong spot
    if (requested_len < data_len) {  // Should be <=, but still wrong logic
        memcpy(out, data, requested_len);  // Still potentially ERROR
    }
}

// =============================================================================
// SECTION 15: Multi-Buffer Confusion
// =============================================================================

// Variant 15.1: Check one buffer, copy from another
void vulnerable_buffer_confusion(const uint8_t* header, size_t header_len,
                                 const uint8_t* body, size_t body_len,
                                 uint16_t copy_len, uint8_t* dest) {
    // Check against header, copy from body
    if (copy_len <= header_len) {
        memcpy(dest, body, copy_len);  // ERROR: checked header_len, not body_len!
    }
}

// Variant 15.2: Use length from struct A to copy from struct B
struct HeaderA { uint32_t length; };
struct DataB { uint8_t data[64]; };

void vulnerable_struct_confusion(const HeaderA* hdr, const DataB* dat, uint8_t* out) {
    // Trust hdr->length to copy from dat
    memcpy(out, dat->data, hdr->length);  // ERROR: hdr->length could be > 64
}

int main() {
    return 0;
}
