// FFmpeg filter graph operations -- deref limitations (xfail)
//
// avfilter_graph_free uses "ensures arg0.deref.@filter_alive = 0" which DTT
// cannot resolve for free functions. Many filter operations depend on the
// graph state established through these deref patterns.
//
// Functions tested: avfilter_graph_alloc, avfilter_graph_free (deref),
//   avfilter_graph_create_filter, avfilter_graph_config,
//   avfilter_graph_parse_ptr, avfilter_graph_parse2,
//   avfilter_get_by_name, avfilter_inout_alloc, avfilter_inout_free,
//   av_buffersrc_add_frame, av_buffersrc_add_frame_flags,
//   av_buffersrc_write_frame, av_buffersink_get_frame,
//   av_buffersink_get_frame_flags, av_buffersink_get_samples

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/frame.h>
}

// Variant 1: Filter graph with create_filter and config
void variant_graph_create_filter() {
    AVFilterGraph *graph = avfilter_graph_alloc();        // @filter_alive = 1

    const AVFilter *buffersrc = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");

    AVFilterContext *src_ctx = nullptr;
    AVFilterContext *sink_ctx = nullptr;
    avfilter_graph_create_filter(&src_ctx, buffersrc, "in",
                                  "video_size=640x480:pix_fmt=0:time_base=1/25",
                                  nullptr, graph);        // requires @filter_alive == 1

    avfilter_graph_create_filter(&sink_ctx, buffersink, "out",
                                  nullptr, nullptr, graph);

    avfilter_graph_config(graph, nullptr);                // requires @filter_alive == 1

    AVFrame *frame = av_frame_alloc();                    // @frame_alive = 1

    av_buffersrc_add_frame(src_ctx, frame);
    av_buffersrc_add_frame_flags(src_ctx, frame, 0);
    av_buffersrc_write_frame(src_ctx, frame);

    av_buffersink_get_frame(sink_ctx, frame);             // requires @frame_alive != 0
    av_buffersink_get_frame_flags(sink_ctx, frame, 0);    // requires @frame_alive != 0
    av_buffersink_get_samples(sink_ctx, frame, 1024);     // requires @frame_alive != 0

    av_frame_free(&frame);
    avfilter_graph_free(&graph);                          // deref: @filter_alive = 0
}

// Variant 2: Filter graph with parse_ptr
void variant_graph_parse_ptr() {
    AVFilterGraph *graph = avfilter_graph_alloc();        // @filter_alive = 1

    AVFilterInOut *inputs  = avfilter_inout_alloc();
    AVFilterInOut *outputs = avfilter_inout_alloc();

    avfilter_graph_parse_ptr(graph, "null",               // requires @filter_alive == 1
                              &inputs, &outputs, nullptr);

    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    avfilter_graph_free(&graph);
}

// Variant 3: Filter graph with parse2
void variant_graph_parse2() {
    AVFilterGraph *graph = avfilter_graph_alloc();        // @filter_alive = 1

    AVFilterInOut *inputs  = nullptr;
    AVFilterInOut *outputs = nullptr;

    avfilter_graph_parse2(graph, "null",                  // requires @filter_alive == 1
                           &inputs, &outputs);

    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    avfilter_graph_config(graph, nullptr);
    avfilter_graph_free(&graph);
}

int main() {
    variant_graph_create_filter();
    variant_graph_parse_ptr();
    variant_graph_parse2();
    return 0;
}
