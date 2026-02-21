// FFmpeg dictionary modification functions -- deref-like limitations (xfail)
//
// av_dict_set, av_dict_set_int, av_dict_copy, and av_dict_free take
// AVDictionary** parameters. The standard FFmpeg pattern initializes
// the dict pointer to nullptr and lets av_dict_set allocate. DTT tracks
// arg0._nonnull through the double pointer, so a nullptr-initialized dict
// triggers a false positive on the nonnull precondition.
//
// Functions tested: av_dict_set, av_dict_set_int, av_dict_copy, av_dict_free

extern "C" {
#include <libavutil/dict.h>
}

// Variant 1: Basic set/get/count/free lifecycle
void variant_dict_lifecycle() {
    AVDictionary *dict = nullptr;

    av_dict_set(&dict, "key1", "value1", 0);
    av_dict_set(&dict, "key2", "value2", 0);
    av_dict_set_int(&dict, "bitrate", 128000, 0);

    av_dict_count(dict);
    av_dict_get(dict, "key1", nullptr, 0);

    av_dict_free(&dict);
}

// Variant 2: Dict copy
void variant_dict_copy() {
    AVDictionary *src = nullptr;
    av_dict_set(&src, "codec", "h264", 0);

    AVDictionary *dst = nullptr;
    av_dict_copy(&dst, src, 0);

    av_dict_free(&dst);
    av_dict_free(&src);
}

int main() {
    variant_dict_lifecycle();
    variant_dict_copy();
    return 0;
}
