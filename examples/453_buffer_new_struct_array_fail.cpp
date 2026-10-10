// Example 453: new[] struct array member OOB — should FAIL
// Tests OOB detection on array members of dynamically allocated structs

struct Packet {
    char payload[32];
};

void test_new_struct_oob() {
    Packet* p = new Packet;

    char* data = p->payload;  // _size=32
    data[32] = 'X';           // FAIL: 32 >= 32 (one past end)  dtt-error {{out of bounds}}

    delete p;
}

int main() {
    test_new_struct_oob();
    return 0;
}
