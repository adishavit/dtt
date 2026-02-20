// FFmpeg I/O context operations -- deref limitations (xfail)
//
// avio_open and avio_open2 use "ensures arg0.deref.@avio_open = 1" and
// avio_closep uses "ensures arg0.deref.@avio_open = 0" which DTT cannot
// resolve for free functions. Operations like avio_read/write/seek require
// @avio_open == 1 which depends on the deref postcondition.
//
// Functions tested: avio_open, avio_open2, avio_close, avio_closep,
//   avio_read, avio_write, avio_seek, avio_size, avio_flush

extern "C" {
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
}

// Variant 1: avio_open -> read/seek/size -> close
void variant_avio_read() {
    AVIOContext *pb = nullptr;
    avio_open(&pb, "input.dat", 0);                       // deref: @avio_open = 1

    unsigned char buf[1024];
    avio_read(pb, buf, sizeof(buf));                       // requires @avio_open == 1
    avio_seek(pb, 0, 0);                                   // requires @avio_open == 1
    avio_size(pb);                                         // requires @avio_open == 1

    avio_close(pb);                                        // @avio_open = 0
}

// Variant 2: avio_open -> write/flush -> closep
void variant_avio_write() {
    AVIOContext *pb = nullptr;
    avio_open(&pb, "output.dat", 1);                       // deref: @avio_open = 1

    const unsigned char data[64] = {};
    avio_write(pb, data, sizeof(data));                    // requires @avio_open == 1
    avio_flush(pb);                                        // requires @avio_open == 1

    avio_closep(&pb);                                      // deref: @avio_open = 0
}

// Variant 3: avio_open2 with options -> close
void variant_avio_open2() {
    AVIOContext *pb = nullptr;
    avio_open2(&pb, "input2.dat", 0, nullptr, nullptr);    // deref: @avio_open = 1

    unsigned char buf[256];
    avio_read(pb, buf, sizeof(buf));
    avio_seek(pb, 100, 0);

    avio_close(pb);
}

int main() {
    variant_avio_read();
    variant_avio_write();
    variant_avio_open2();
    return 0;
}
