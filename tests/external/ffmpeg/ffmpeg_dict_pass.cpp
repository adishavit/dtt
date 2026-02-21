// FFmpeg dictionary query functions -- correct usage (should COMPILE)
//
// Tests AVDictionary query operations that take a direct AVDictionary*
// (single pointer), not AVDictionary** (double pointer). The double-pointer
// functions (av_dict_set, av_dict_free, etc.) have deref-like nonnull
// tracking issues and are tested in ffmpeg_dict_modify_xfail.cpp.
//
// Functions tested: av_dict_count, av_dict_get

extern "C" {
#include <libavutil/dict.h>
}

// Variant 1: av_dict_count on null dict (no preconditions)
void variant_dict_count_null() {
    AVDictionary *dict = nullptr;
    av_dict_count(dict);
    av_dict_count(nullptr);
}

// Variant 2: av_dict_get with string key (requires arg1 nonnull)
void variant_dict_get() {
    AVDictionary *dict = nullptr;
    av_dict_get(dict, "key1", nullptr, 0);
    av_dict_get(dict, "nonexistent", nullptr, 0);
    av_dict_get(dict, "", nullptr, 2);
}

int main() {
    variant_dict_count_null();
    variant_dict_get();
    return 0;
}
