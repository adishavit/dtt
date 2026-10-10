// FFmpeg codec context lifecycle -- operations before open (should FAIL to compile)
//
// Tests that DTT catches codec send/receive operations when the codec context
// is only allocated (@codec_state == 1) but not yet opened (requires 2).

extern "C" {
#include <libavcodec/avcodec.h>
}

// Variant 1: send_packet before open2 (state 1, requires 2)
void variant_send_packet_before_open() {
    const AVCodec *dec = avcodec_find_decoder((enum AVCodecID)0);
    AVCodecContext *dec_ctx = avcodec_alloc_context3(dec);  // @codec_state = 1
    if (!dec_ctx) return;
    // Skipping avcodec_open2 -- codec not opened

    AVPacket *pkt = av_packet_alloc();                      // @packet_alive = 1
    if (!pkt) return;

    // DTT ERROR: requires arg0.@codec_state == 2 (required state is not established in enforce mode)
    avcodec_send_packet(dec_ctx, pkt);  // dtt-error {{requires @codec_state}}

    av_packet_free(&pkt);
    avcodec_free_context(&dec_ctx);
}

// Variant 2: receive_frame before open2 (state 1, requires 2)
void variant_receive_frame_before_open() {
    const AVCodec *dec = avcodec_find_decoder((enum AVCodecID)0);
    AVCodecContext *dec_ctx = avcodec_alloc_context3(dec);  // @codec_state = 1
    if (!dec_ctx) return;
    // Skipping avcodec_open2 -- codec not opened

    AVFrame *frm = av_frame_alloc();                        // @frame_alive = 1
    if (!frm) return;

    // DTT ERROR: requires arg0.@codec_state == 2 (required state is not established in enforce mode)
    avcodec_receive_frame(dec_ctx, frm);  // dtt-error {{requires @codec_state}}

    av_frame_free(&frm);
    avcodec_free_context(&dec_ctx);
}

int main() {
    variant_send_packet_before_open();
    variant_receive_frame_before_open();
    return 0;
}
