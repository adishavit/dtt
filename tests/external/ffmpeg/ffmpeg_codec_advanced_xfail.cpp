// FFmpeg codec context advanced operations -- deref + state limitations (xfail)
//
// avcodec_free_context uses "ensures arg0.deref.@codec_state = 0" which DTT
// cannot resolve for free functions. The additional codec functions tested here
// depend on state transitions that flow through deref postconditions.
//
// Functions tested: avcodec_free_context (deref), avcodec_parameters_to_context,
//   avcodec_parameters_from_context, avcodec_find_decoder_by_name,
//   avcodec_find_encoder_by_name, avcodec_close, avcodec_flush_buffers

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

// Variant 1: parameters_to_context with named codec lookup
void variant_params_to_context() {
    const AVCodec *dec = avcodec_find_decoder_by_name("h264");
    AVCodecContext *dec_ctx = avcodec_alloc_context3(dec);  // @codec_state = 1

    // In a real workflow, par comes from a stream; simulate with an allocated context
    AVFormatContext *fmt_ctx = avformat_alloc_context();
    avformat_open_input(&fmt_ctx, "input.mp4", nullptr, nullptr);
    avformat_find_stream_info(fmt_ctx, nullptr);

    // avcodec_parameters_to_context requires @codec_state == 1
    avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[0]->codecpar);

    avcodec_open2(dec_ctx, dec, nullptr);                   // @codec_state = 2
    avcodec_flush_buffers(dec_ctx);                         // requires @codec_state == 2
    avcodec_close(dec_ctx);                                 // requires 2, ensures 1
    avcodec_free_context(&dec_ctx);                         // deref: @codec_state = 0

    avformat_close_input(&fmt_ctx);
}

// Variant 2: parameters_from_context with named encoder lookup
void variant_params_from_context() {
    const AVCodec *enc = avcodec_find_encoder_by_name("libx264");
    AVCodecContext *enc_ctx = avcodec_alloc_context3(enc);  // @codec_state = 1
    avcodec_open2(enc_ctx, enc, nullptr);                   // @codec_state = 2

    AVFormatContext *out_ctx = nullptr;
    avformat_alloc_output_context2(&out_ctx, nullptr, "mp4", "output.mp4");
    AVStream *st = avformat_new_stream(out_ctx, enc);

    // avcodec_parameters_from_context copies encoder settings to stream params
    avcodec_parameters_from_context(st->codecpar, enc_ctx);

    avcodec_free_context(&enc_ctx);                         // deref: @codec_state = 0
    avformat_free_context(out_ctx);
}

int main() {
    variant_params_to_context();
    variant_params_from_context();
    return 0;
}
