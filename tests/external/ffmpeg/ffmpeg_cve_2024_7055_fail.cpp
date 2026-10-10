// CVE-2024-7055: Use-after-free on format context (should FAIL to compile)
//
// CVE-2024-7055 involves a heap buffer overflow in pnm_decode_frame, part of
// a broader class of bugs where format/codec state is used after the context
// has been torn down. DTT catches this pattern because avformat operations
// require specific @format_state values that become 0 after close_input.
//
// Reference: https://nvd.nist.gov/vuln/detail/CVE-2024-7055

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

// Variant 1: av_find_best_stream after close_input
void variant_best_stream_after_close() {
    AVFormatContext *fmt_ctx = avformat_alloc_context(); // @format_state = 1
    avformat_open_input(&fmt_ctx, "input.mp4",          // @format_state = 2
                        nullptr, nullptr);
    avformat_find_stream_info(fmt_ctx, nullptr);         // @format_state = 3

    avformat_close_input(&fmt_ctx);                      // @format_state = 0

    // DTT ERROR: requires arg0.@format_state == 3 (required state is not established in enforce mode)
    av_find_best_stream(fmt_ctx, (enum AVMediaType)0, -1, -1, nullptr, 0);  // dtt-error {{@format_state}}
}

int main() {
    variant_best_stream_after_close();
    return 0;
}
