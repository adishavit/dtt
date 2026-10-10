// FFmpeg format context lifecycle -- incorrect usage (should FAIL to compile)
//
// Tests that DTT catches operations on a format context in an invalid state.
// After avformat_close_input, @format_state == 0, but find_stream_info
// requires @format_state == 2 and av_read_frame requires @format_state == 3.

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

// Variant 1: find_stream_info after close_input (state 0, requires 2)
void variant_find_streams_after_close() {
    AVFormatContext *fmt_ctx = avformat_alloc_context(); // @format_state = 1
    avformat_open_input(&fmt_ctx, "input.mp4",          // @format_state = 2
                        nullptr, nullptr);
    avformat_close_input(&fmt_ctx);                      // @format_state = 0

    // DTT ERROR: requires arg0.@format_state == 2 (required state is not established in enforce mode)
    avformat_find_stream_info(fmt_ctx, nullptr);  // dtt-error {{@format_state}}
}

// Variant 2: av_read_frame after close_input (state 0, requires 3)
void variant_read_frame_after_close() {
    AVFormatContext *fmt_ctx = avformat_alloc_context(); // @format_state = 1
    avformat_open_input(&fmt_ctx, "input.mp4",          // @format_state = 2
                        nullptr, nullptr);
    avformat_find_stream_info(fmt_ctx, nullptr);         // @format_state = 3

    AVPacket *pkt = av_packet_alloc();                   // @packet_alive = 1
    if (!pkt) return;

    avformat_close_input(&fmt_ctx);                      // @format_state = 0

    // DTT ERROR: requires arg0.@format_state == 3 (required state is not established in enforce mode)
    av_read_frame(fmt_ctx, pkt);  // dtt-error {{@format_state}}

    av_packet_free(&pkt);
}

int main() {
    variant_find_streams_after_close();
    variant_read_frame_after_close();
    return 0;
}
