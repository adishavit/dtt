// FFmpeg packet/frame advanced operations -- direct arg0 patterns (should COMPILE)
//
// These functions use direct arg0 preconditions (not deref), so DTT can fully
// track their ghost field state. Tests exercise clone, ref, move_ref, copy,
// and rescale operations on packets and frames.
//
// Functions tested: av_packet_clone, av_packet_ref, av_packet_move_ref,
//   av_packet_make_refcounted, av_packet_rescale_ts, av_new_packet,
//   av_init_packet, av_frame_clone, av_frame_ref, av_frame_move_ref,
//   av_frame_copy, av_frame_copy_props

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

// Variant 1: Packet clone and ref operations
void variant_packet_clone_ref() {
    AVPacket *src = av_packet_alloc();                   // @packet_alive = 1
    AVPacket *clone = av_packet_clone(src);              // @packet_alive = 1 (new)

    AVPacket *dst = av_packet_alloc();                   // @packet_alive = 1
    av_packet_ref(dst, src);                             // requires both alive

    av_packet_free(&dst);
    av_packet_free(&clone);
    av_packet_free(&src);
}

// Variant 2: Packet move_ref and make_refcounted
void variant_packet_move_refcounted() {
    AVPacket *pkt = av_packet_alloc();                   // @packet_alive = 1
    av_packet_make_refcounted(pkt);                      // requires @packet_alive != 0

    AVPacket *dst = av_packet_alloc();                   // @packet_alive = 1
    av_packet_move_ref(dst, pkt);                        // requires both alive

    av_packet_free(&dst);
    av_packet_free(&pkt);
}

// Variant 3: Packet rescale_ts
void variant_packet_rescale() {
    AVPacket *pkt = av_packet_alloc();                   // @packet_alive = 1
    AVRational src_tb = {1, 90000};
    AVRational dst_tb = {1, 48000};
    av_packet_rescale_ts(pkt, src_tb, dst_tb);           // requires @packet_alive != 0
    av_packet_free(&pkt);
}

// Variant 4: av_new_packet and av_init_packet
void variant_new_init_packet() {
    AVPacket pkt_on_stack = {};
    av_init_packet(&pkt_on_stack);                       // requires arg0._nonnull != 0

    AVPacket *pkt = av_packet_alloc();                   // @packet_alive = 1
    av_new_packet(pkt, 1024);                            // requires arg0._nonnull != 0
    av_packet_free(&pkt);
}

// Variant 5: Frame clone and ref operations
void variant_frame_clone_ref() {
    AVFrame *src = av_frame_alloc();                     // @frame_alive = 1
    AVFrame *clone = av_frame_clone(src);                // @frame_alive = 1 (new)

    AVFrame *dst = av_frame_alloc();                     // @frame_alive = 1
    av_frame_ref(dst, src);                              // requires both alive

    av_frame_free(&dst);
    av_frame_free(&clone);
    av_frame_free(&src);
}

// Variant 6: Frame move_ref
void variant_frame_move() {
    AVFrame *src = av_frame_alloc();                     // @frame_alive = 1
    AVFrame *dst = av_frame_alloc();                     // @frame_alive = 1
    av_frame_move_ref(dst, src);                         // requires both alive

    av_frame_free(&dst);
    av_frame_free(&src);
}

// Variant 7: Frame copy and copy_props
void variant_frame_copy() {
    AVFrame *src = av_frame_alloc();                     // @frame_alive = 1
    AVFrame *dst = av_frame_alloc();                     // @frame_alive = 1

    av_frame_copy(dst, src);                             // requires both alive
    av_frame_copy_props(dst, src);                       // requires both alive

    av_frame_free(&dst);
    av_frame_free(&src);
}

int main() {
    variant_packet_clone_ref();
    variant_packet_move_refcounted();
    variant_packet_rescale();
    variant_new_init_packet();
    variant_frame_clone_ref();
    variant_frame_move();
    variant_frame_copy();
    return 0;
}
