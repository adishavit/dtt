// Example 452: new[] of struct with array member — should COMPILE
// Tests that array members are tracked on dynamically allocated structs

struct Packet {
    int header;
    char payload[64];
    int checksum;
};

void test_new_struct_array() {
    Packet* packets = new Packet[10];  // 10 packets

    // Access payload of first packet
    char* p = packets[0].payload;  // _size=64
    p[63] = '\0';                  // OK: last char

    // Access payload of last packet
    char* q = packets[9].payload;  // _size=64
    q[0] = 'X';                    // OK: first char

    delete[] packets;
}

int main() {
    test_new_struct_array();
    return 0;
}
