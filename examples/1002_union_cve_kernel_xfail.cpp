// 1002_union_cve_kernel_fail.cpp - Linux kernel-style type confusion patterns
// Based on BleedingTooth (L2CAP), nft_set_elem_init, CVE-2018-9568 (WrongZone)
// Pattern: Kernel data structure type confusion leading to privilege escalation

// L2CAP channel data union (BleedingTooth CVE pattern)
// chan->data is used for arbitrary data types
struct L2capSock { int fd; char buffer[64]; };
struct L2capSmpChan { void* smp_context; int flags; };
struct L2capAttChan { void* att_handler; int mtu; };

union ChannelData {
    L2capSock sock;
    L2capSmpChan smp;
    L2capAttChan att;
};

struct L2capChannel {
    int type;  // 0=sock, 1=smp, 2=att
    ChannelData data;
};

// Variant 1: Bluetooth L2CAP type confusion (BleedingTooth pattern)
void test_l2cap_confusion(L2capChannel* chan) {
    // Malicious packet sets type=sock but data is attacker-controlled
    // Code reads as SMP channel
    void* ctx = chan->data.smp.smp_context;  // FAIL: sock.fd interpreted as pointer
    (void)ctx;
}

// Variant 2: ATT handler as sock buffer
void test_l2cap_att_sock(L2capChannel* chan) {
    chan->type = 0;  // Claim it's a socket
    chan->data.sock.fd = 42;
    // Later code treats as ATT channel
    void* handler = chan->data.att.att_handler;  // FAIL: fd interpreted as handler ptr
    (void)handler;
}

// Netfilter set element (nft_set_elem_init type confusion pattern)
union NftData {
    struct { unsigned int data; } verdict;
    struct { void* chain; } jumpchain;
    unsigned int value;
    void* ptr;
};

struct NftSetElem {
    int type;  // NFT_DATA_VERDICT, NFT_DATA_VALUE, etc.
    NftData data;
};

// Variant 3: Netfilter verdict/chain confusion
void test_nft_confusion(NftSetElem* elem) {
    // Attacker claims type is VERDICT (integer)
    elem->type = 0;  // VERDICT type
    elem->data.verdict.data = 0x41414141;
    // But code treats as JUMPCHAIN (pointer)
    void* chain = elem->data.jumpchain.chain;  // FAIL: int as pointer
    (void)chain;
}

// Variant 4: Netfilter buffer overflow via confusion
void test_nft_overflow(NftSetElem* elem, unsigned int size) {
    // Type confusion leads to wrong size calculation
    elem->type = 0;  // Claims value type (4 bytes)
    // But processes as ptr type (8 bytes on 64-bit)
    for (unsigned i = 0; i < size; i += sizeof(void*)) {
        void** ptrs = (void**)&elem->data;
        ptrs[i / sizeof(void*)] = nullptr;  // May overflow
    }
}

// WrongZone pattern (CVE-2018-9568) - sk_buff type confusion
union SkbData {
    struct { int protocol; void* dev; } net;
    struct { void* sk; int cb[48]; } transport;
    unsigned char data[64];
};

struct SkBuff {
    int type;
    SkbData content;
};

// Variant 5: sk_buff network/transport confusion
void test_skb_confusion(SkBuff* skb) {
    skb->type = 0;  // Network layer
    skb->content.net.protocol = 0x0800;  // IP
    skb->content.net.dev = (void*)0x41414141;
    // Code processes as transport layer
    void* sock = skb->content.transport.sk;  // FAIL: dev pointer as socket
    (void)sock;
}

// Cred structure confusion (privilege escalation pattern)
union CredUnion {
    struct { int uid; int gid; } user;
    struct { int euid; int egid; int fsuid; } effective;
};

struct TaskCred {
    int type;  // 0=user, 1=effective
    CredUnion cred;
};

// Variant 6: Credential confusion for privilege escalation
void test_cred_confusion(TaskCred* tc) {
    tc->type = 0;  // Claim user type
    tc->cred.user.uid = 1000;  // Regular user
    // Code checks effective uid without verifying type
    int euid = tc->cred.effective.euid;  // FAIL: uid read as euid
    if (euid == 0) {
        // Grant root - but euid was actually uid=1000
    }
    (void)euid;
}

// File descriptor union (common in syscall handling)
union FdData {
    struct { int fd; int flags; } file;
    struct { void* sock; int state; } socket;
    struct { void* pipe_info; } pipe;
};

struct FdEntry {
    int type;
    FdData data;
};

// Variant 7: File/socket confusion
void test_fd_confusion(FdEntry* entry) {
    entry->type = 0;  // File type
    entry->data.file.fd = 3;
    // Code assumes socket
    void* sock = entry->data.socket.sock;  // FAIL: fd as pointer
    (void)sock;
}

// Variant 8: Pipe/socket confusion
void test_pipe_socket(FdEntry* entry) {
    entry->type = 2;  // Pipe type
    entry->data.pipe.pipe_info = nullptr;
    // Code processes as socket
    int state = entry->data.socket.state;  // FAIL: reading garbage
    (void)state;
}

int main() {
    L2capChannel chan;
    chan.type = 0;
    test_l2cap_confusion(&chan);
    test_l2cap_att_sock(&chan);

    NftSetElem elem;
    elem.type = 0;
    test_nft_confusion(&elem);
    test_nft_overflow(&elem, 16);

    SkBuff skb;
    skb.type = 0;
    test_skb_confusion(&skb);

    TaskCred tc;
    tc.type = 0;
    test_cred_confusion(&tc);

    FdEntry entry;
    entry.type = 0;
    test_fd_confusion(&entry);
    test_pipe_socket(&entry);
    return 0;
}
