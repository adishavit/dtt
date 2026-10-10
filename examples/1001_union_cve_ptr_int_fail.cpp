// 1001_union_cve_ptr_int_fail.cpp - Pointer/Integer confusion patterns
// Based on COMFUSION research, CWE-843 pointer aliasing vulnerabilities
// Pattern: Union member interpreted as pointer when it's actually an integer

#include <cstdint>

// Classic pointer/integer union (common in low-level code)
union PtrOrInt {
    void* ptr;
    long intVal;
    unsigned long uintVal;
};

// Variant 1: AAR - Arbitrary Address Read via int-as-pointer
void test_arbitrary_read(PtrOrInt u) {
    // Attacker sets intVal to target address, code treats as pointer
    u.intVal = 0x41414141;  // Attacker-controlled "address"
    char c = *((char*)u.ptr);  // FAIL: reading from attacker-controlled address (AAR)
    (void)c;
}

// Variant 2: AAW - Arbitrary Address Write via int-as-pointer
void test_arbitrary_write(PtrOrInt u, char value) {
    // Attacker sets intVal to target address
    u.intVal = 0x41414141;
    *((char*)u.ptr) = value;  // FAIL: writing to attacker-controlled address (AAW)
}

// Variant 3: Function pointer confusion (code execution)
union FuncOrInt {
    void (*func)();
    long intVal;
};

void test_func_ptr_confusion(FuncOrInt u) {
    u.intVal = 0x41414141;  // Attacker-controlled "function address"
    u.func();  // FAIL: calling attacker-controlled address (RCE)
}

// Socket address structure (based on CVE-2012-6657, CVE-2022-31696)
union SockAddrUnion {
    struct { short family; char data[14]; } generic;
    struct { short family; unsigned short port; unsigned int addr; } inet;
    struct { short family; char path[108]; } unix_sock;
};

// Variant 4: Socket address type confusion (VMware ESXi CVE-2022-31696 pattern)
void test_sockaddr_confusion(SockAddrUnion* sa) {
    // Code assumes inet but could be unix
    unsigned int addr = sa->inet.addr;  // FAIL: if unix, reading part of path
    (void)addr;
}

// Variant 5: Socket path buffer overflow via confusion
void test_sockaddr_overflow(SockAddrUnion* sa, const char* input) {
    // Assumes generic.data (14 bytes) but is unix.path (108 bytes)
    // Or vice versa - writes 108 bytes into 14-byte buffer concept
    for (int i = 0; i < 14 && input[i]; i++) {
        sa->generic.data[i] = input[i];  // If treating unix as generic, truncated
    }
}

// COM BSTR confusion (based on WalletService vulnerability in COMFUSION)
struct BstrVariant {
    int type;  // 0 = int, 8 = BSTR (string pointer)
    union {
        int intVal;
        char* bstrVal;
    };
};

// Variant 6: BSTR pointer from int value
std::intptr_t test_bstr_as_int(BstrVariant* v) {
    v->type = 0;  // Mark as int
    v->intVal = 0x12345678;  // Set integer value
    // Later code assumes BSTR without checking type
    return reinterpret_cast<std::intptr_t>(v->bstrVal);  // FAIL: returning int value as pointer value (info leak)
}

// Variant 7: Integer dereference as string
void test_int_as_bstr(BstrVariant* v) {
    v->type = 0;  // Actually an int
    v->intVal = 0x12345678;
    // Code doesn't check type, assumes bstrVal
    char c = v->bstrVal[0];  // FAIL: dereferencing int as pointer
    (void)c;
}

// Network packet header union (common in protocol parsers)
union PacketData {
    struct { unsigned int srcIP; unsigned int dstIP; } ipHeader;
    struct { unsigned short srcPort; unsigned short dstPort; } tcpHeader;
    unsigned char rawBytes[8];
};

// Variant 8: IP/TCP header confusion
void test_protocol_confusion(PacketData* pkt, int type) {
    (void)type;  // Ignored!
    // Always reads as IP header regardless of actual type
    unsigned int src = pkt->ipHeader.srcIP;  // FAIL: could be TCP data
    (void)src;
}

int main() {
    PtrOrInt poi;
    poi.intVal = 0;
    test_arbitrary_read(poi);
    test_arbitrary_write(poi, 'X');

    FuncOrInt foi;
    foi.intVal = 0;
    test_func_ptr_confusion(foi);

    SockAddrUnion sa;
    sa.generic.family = 0;
    test_sockaddr_confusion(&sa);
    test_sockaddr_overflow(&sa, "test");

    BstrVariant bv;
    bv.type = 0;
    bv.intVal = 0;
    test_bstr_as_int(&bv);
    test_int_as_bstr(&bv);

    PacketData pkt;
    pkt.ipHeader.srcIP = 0;
    test_protocol_confusion(&pkt, 0);
    return 0;
}
