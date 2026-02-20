// FFmpeg format context advanced operations -- deref limitations (xfail)
//
// These functions use "ensures arg0.deref.@format_state = N" postconditions
// which DTT cannot currently resolve for free functions. The test exercises
// multi-step input/output format workflows that depend on deref results.
//
// Functions tested: avformat_open_input, avformat_close_input,
//   avformat_alloc_output_context2, avformat_write_header, av_write_trailer,
//   av_interleaved_write_frame, av_write_frame, avformat_new_stream,
//   av_find_best_stream, av_seek_frame, avformat_seek_file, av_dump_format

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

// Variant 1: Full input lifecycle with seek operations
void variant_input_seek() {
    AVFormatContext *fmt_ctx = avformat_alloc_context();   // @format_state = 1
    avformat_open_input(&fmt_ctx, "input.mp4",            // deref: @format_state = 2
                        nullptr, nullptr);
    avformat_find_stream_info(fmt_ctx, nullptr);           // @format_state = 3

    av_dump_format(fmt_ctx, 0, "input.mp4", 0);

    av_find_best_stream(fmt_ctx, (enum AVMediaType)0, -1, -1, nullptr, 0);
    av_seek_frame(fmt_ctx, 0, 0, 0);
    avformat_seek_file(fmt_ctx, 0, 0, 0, 100000, 0);

    avformat_close_input(&fmt_ctx);                        // deref: @format_state = 0
}

// Variant 2: Full output lifecycle with write operations
void variant_output_write() {
    AVFormatContext *out_ctx = nullptr;
    avformat_alloc_output_context2(&out_ctx, nullptr,      // deref: @format_state = 1
                                    "mp4", "output.mp4");

    const AVCodec *enc = avcodec_find_encoder((enum AVCodecID)0);
    AVStream *st = avformat_new_stream(out_ctx, enc);      // requires @format_state == 1

    avformat_write_header(out_ctx, nullptr);               // requires @format_state == 1, ensures 2

    AVPacket *pkt = av_packet_alloc();
    av_interleaved_write_frame(out_ctx, pkt);              // requires @format_state == 2
    av_write_frame(out_ctx, pkt);                          // requires @format_state == 2
    av_packet_free(&pkt);

    av_write_trailer(out_ctx);                             // requires @format_state == 2
    avformat_free_context(out_ctx);
}

int main() {
    variant_input_seek();
    variant_output_write();
    return 0;
}
