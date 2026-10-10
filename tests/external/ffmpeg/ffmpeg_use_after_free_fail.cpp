// FFmpeg use-after-free on packets/frames -- CVE-2024-22860 pattern (should FAIL to compile)
//
// Tests that DTT catches operations on packets and frames after they have been
// freed. This is the class of bug behind CVE-2024-22860 (buffer overflow in
// fill_audiodata) where freed frame data is accessed.
//
// Reference: https://nvd.nist.gov/vuln/detail/CVE-2024-22860

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

// Variant 1: av_frame_get_buffer after av_frame_free
void variant_frame_use_after_free() {
    AVFrame *frame = av_frame_alloc();   // @frame_alive = 1
    if (!frame) return;
    av_frame_free(&frame);              // @frame_alive = 0

    // DTT ERROR: requires arg0.@frame_alive != 0, but @frame_alive == 0
    av_frame_get_buffer(frame, 0);  // dtt-error {{requires @frame_alive}}
}

// Variant 2: av_packet_unref after av_packet_free
void variant_packet_use_after_free() {
    AVPacket *pkt = av_packet_alloc();   // @packet_alive = 1
    if (!pkt) return;
    av_packet_free(&pkt);               // @packet_alive = 0

    // DTT ERROR: requires arg0.@packet_alive != 0, but @packet_alive == 0
    av_packet_unref(pkt);  // dtt-error {{requires @packet_alive}}
}

int main() {
    variant_frame_use_after_free();
    variant_packet_use_after_free();
    return 0;
}
