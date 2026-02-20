// FFmpeg codec context lifecycle -- correct usage patterns (should COMPILE)
//
// Tests that DTT ghost field @codec_state correctly tracks the codec context
// through allocation, opening, send/receive operations, and cleanup.

extern "C" {
#include <libavcodec/avcodec.h>
}

// Variant 1 (decode): alloc -> open2 -> send_packet + receive_frame -> free
void variant_decode_lifecycle() {
    const AVCodec *dec = avcodec_find_decoder((enum AVCodecID)0);
    AVCodecContext *dec_ctx = avcodec_alloc_context3(dec);  // @codec_state = 1
    avcodec_open2(dec_ctx, dec, nullptr);                   // @codec_state = 2

    AVPacket *pkt = av_packet_alloc();                      // @packet_alive = 1
    AVFrame  *frm = av_frame_alloc();                       // @frame_alive = 1

    avcodec_send_packet(dec_ctx, pkt);                      // requires @codec_state == 2
    avcodec_receive_frame(dec_ctx, frm);                    // requires @codec_state == 2

    av_frame_free(&frm);                                    // @frame_alive = 0
    av_packet_free(&pkt);                                   // @packet_alive = 0
    avcodec_free_context(&dec_ctx);                         // @codec_state = 0
}

// Variant 2 (encode): alloc -> open2 -> send_frame + receive_packet -> free
void variant_encode_lifecycle() {
    const AVCodec *enc = avcodec_find_encoder((enum AVCodecID)0);
    AVCodecContext *enc_ctx = avcodec_alloc_context3(enc);  // @codec_state = 1
    avcodec_open2(enc_ctx, enc, nullptr);                   // @codec_state = 2

    AVFrame  *frm = av_frame_alloc();                       // @frame_alive = 1
    AVPacket *pkt = av_packet_alloc();                      // @packet_alive = 1

    avcodec_send_frame(enc_ctx, frm);                       // requires @codec_state == 2
    avcodec_receive_packet(enc_ctx, pkt);                   // requires @codec_state == 2

    av_packet_free(&pkt);                                   // @packet_alive = 0
    av_frame_free(&frm);                                    // @frame_alive = 0
    avcodec_free_context(&enc_ctx);                         // @codec_state = 0
}

int main() {
    variant_decode_lifecycle();
    variant_encode_lifecycle();
    return 0;
}
