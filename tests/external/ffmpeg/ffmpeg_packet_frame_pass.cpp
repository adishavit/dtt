// FFmpeg packet/frame lifecycle -- correct usage patterns (should COMPILE)
//
// Tests that DTT ghost fields @packet_alive and @frame_alive correctly track
// the lifecycle of AVPacket and AVFrame objects through alloc, use, and free.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

// Variant 1: av_packet_alloc -> unref -> free
void variant_packet_lifecycle() {
    AVPacket *pkt = av_packet_alloc();   // @packet_alive = 1
    av_packet_unref(pkt);               // requires @packet_alive != 0
    av_packet_free(&pkt);               // @packet_alive = 0
}

// Variant 2: av_frame_alloc -> get_buffer -> unref -> free
void variant_frame_with_buffer() {
    AVFrame *frame = av_frame_alloc();   // @frame_alive = 1
    frame->format = 0;
    frame->width  = 640;
    frame->height = 480;
    av_frame_get_buffer(frame, 0);       // requires @frame_alive != 0
    av_frame_unref(frame);              // requires @frame_alive != 0
    av_frame_free(&frame);              // @frame_alive = 0
}

// Variant 3: av_frame_alloc -> make_writable -> free
void variant_frame_make_writable() {
    AVFrame *frame = av_frame_alloc();   // @frame_alive = 1
    av_frame_make_writable(frame);       // requires @frame_alive != 0
    av_frame_free(&frame);              // @frame_alive = 0
}

int main() {
    variant_packet_lifecycle();
    variant_frame_with_buffer();
    variant_frame_make_writable();
    return 0;
}
