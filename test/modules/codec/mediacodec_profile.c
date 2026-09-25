/* Copyright (C) 2026 VLC authors and VideoLAN
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#undef NDEBUG
#include <assert.h>

#include "../../../modules/codec/omxil/mediacodec_profile.h"

static void TestPatterns(void)
{
    static const struct
    {
        const char *pattern;
        const char *name;
        bool ignore_case;
        bool matches;
    } cases[] = {
        { "", "", false, true },
        { "", "OMX.vendor.hevc", false, false },
        { "*", "", false, true },
        { "**", "", false, true },
        { "?", "", false, false },
        { "*?", "", false, false },
        { "*", "OMX.vendor.hevc", false, true },
        { "OMX.vendor.hevc", "OMX.vendor.hevc", false, true },
        { "OMX.vendor.hevc", "OMX.vendor.avc", false, false },
        { "OMX.vendor.hevc", "OMX.vendor.hevc.extra", false, false },
        { "OMX.vendor.hevc.extra", "OMX.vendor.hevc", false, false },
        { "OMX.*", "OMX.vendor.hevc", false, true },
        { "*.hevc", "OMX.vendor.hevc", false, true },
        { "OMX.*.hevc", "OMX.vendor.hevc", false, true },
        { "OMX.*.hevc", "OMX..hevc", false, true },
        { "OMX.*.hevc", "c2.vendor.hevc", false, false },
        { "OMX.?.hevc", "OMX.a.hevc", false, true },
        { "OMX.?.hevc", "OMX.ab.hevc", false, false },
        { "OMX.?.hevc", "OMX..hevc", false, false },
        { "OMX.??.hevc", "OMX.ab.hevc", false, true },
        { "OMX.*hevc", "OMX.hevchevchevc", false, true },
        { "OMX.*hevc", "OMX.hevchevchevcx", false, false },
        { "*ab*cd", "ababcabcd", false, true },
        { "*ab*cd", "ababcabcdx", false, false },
        { "*a*b?c*", "zaaabxcend", false, true },
        { "**OMX.***.hevc**", "OMX.vendor.hevc", false, true },
        { "*a", "*ba", false, true },
        { "*?*?*", "ab", false, true },
        { "*?*?*", "a", false, false },
        { "omx.vendor.hevc", "OMX.vendor.hevc", false, false },
        { "omx.vendor.hevc", "OMX.vendor.hevc", true, true },
        { "OMX.*.HEVC", "omx.vendor.hevc", false, false },
        { "OMX.*.HEVC", "omx.vendor.hevc", true, true },
        /* These are glob patterns, not regular expressions. */
        { "^OMX.*", "OMX.vendor.hevc", false, false },
        { "OMX.[ab].hevc", "OMX.a.hevc", false, false },
        { "OMX.[ab].hevc", "OMX.[ab].hevc", false, true },
    };

    for (size_t i = 0; i < ARRAY_SIZE(cases); ++i)
        assert(MediaCodec_MatchDecoderPattern(cases[i].pattern,
                   strlen(cases[i].pattern), cases[i].name,
                   strlen(cases[i].name), cases[i].ignore_case)
               == cases[i].matches);
}

static void TestLists(void)
{
    static const struct
    {
        const char *patterns;
        const char *name;
        bool ignore_case;
        bool matches;
    } cases[] = {
        { NULL, "OMX.vendor.hevc", false, false },
        { "", "OMX.vendor.hevc", false, false },
        { ",,,", "OMX.vendor.hevc", false, false },
        { " \t, ,\t,", "OMX.vendor.hevc", false, false },
        { NULL, "", false, false },
        { "", "", false, false },
        { ",,", "", false, false },
        { "*", "", false, true },
        { "?", "", false, false },
        { "OMX.vendor.hevc", "OMX.vendor.hevc", false, true },
        { "OMX.vendor.avc", "OMX.vendor.hevc", false, false },
        { "OMX.vendor.avc,OMX.vendor.hevc", "OMX.vendor.hevc", false, true },
        { "OMX.vendor.hevc,OMX.vendor.avc", "OMX.vendor.hevc", false, true },
        { "OMX.vendor.avc,c2.vendor.hevc", "OMX.vendor.hevc", false, false },
        { ",,OMX.vendor.hevc,,", "OMX.vendor.hevc", false, true },
        { " \tOMX.*.hevc\t ", "OMX.vendor.hevc", false, true },
        { "x, \tOMX.*.hevc\t , y", "OMX.vendor.hevc", false, true },
        { "x, * ,y", "OMX.vendor.hevc", false, true },
        { "OMX. vendor.hevc", "OMX.vendor.hevc", false, false },
        { "OMX.VENDOR.HEVC", "OMX.vendor.hevc", false, false },
        { "OMX.VENDOR.HEVC", "OMX.vendor.hevc", true, true },
    };

    for (size_t i = 0; i < ARRAY_SIZE(cases); ++i)
        assert(MediaCodec_MatchDecoderList(cases[i].patterns, cases[i].name,
                   strlen(cases[i].name), cases[i].ignore_case)
               == cases[i].matches);
}

static void TestBoundedBuffers(void)
{
    /* JNI decoder names and pattern slices need not be NUL-terminated. */
    const char name[] = { 'O', 'M', 'X', '.', 'a' };
    const char pattern[] = { 'O', 'M', 'X', '.', '?' };
    assert(MediaCodec_MatchDecoderPattern(pattern, sizeof(pattern),
                                         name, sizeof(name), false));
    assert(MediaCodec_MatchDecoderList("OMX.?", name, sizeof(name), false));
    assert(!MediaCodec_MatchDecoderList("OMX.??", name, sizeof(name), false));
    assert(MediaCodec_MatchDecoderPattern("OMX.*ignored", 5,
                                         name, sizeof(name), false));
    assert(MediaCodec_MatchDecoderList("OMX.*", "OMX.aignored", 5, false));

    char long_name[4096];
    char long_pattern[sizeof(long_name) + 2];
    memset(long_name, 'a', sizeof(long_name));
    memset(long_pattern, 'a', sizeof(long_pattern));
    long_pattern[sizeof(long_name)] = '\0';
    assert(MediaCodec_MatchDecoderPattern(long_pattern, sizeof(long_name),
                                         long_name, sizeof(long_name), false));
    assert(MediaCodec_MatchDecoderList(long_pattern, long_name,
                                      sizeof(long_name), false));
    assert(!MediaCodec_MatchDecoderList(long_pattern, long_name,
                                       sizeof(long_name) - 1, false));

    /* Exercise retry at the end of a long name, including a failed suffix. */
    long_pattern[0] = '*';
    long_pattern[sizeof(long_name) - 1] = 'b';
    long_pattern[sizeof(long_name)] = '*';
    long_pattern[sizeof(long_name) + 1] = '\0';
    assert(!MediaCodec_MatchDecoderList(long_pattern, long_name,
                                       sizeof(long_name), false));
    long_name[sizeof(long_name) - 1] = 'b';
    assert(MediaCodec_MatchDecoderList(long_pattern, long_name,
                                      sizeof(long_name), false));
}

static void TestDolbyDefaults(void)
{
    const char *patterns =
        "*dolby.decoder.hevc,OMX.realtek.video.dec.dvhe.stn";
    const char *names[] = {
        "OMX.vendor.dolby.decoder.hevc",
        "c2.vendor.dolby.decoder.hevc",
        "OMX.realtek.video.dec.dvhe.stn",
        "omx.REALTEK.video.dec.DVHE.stn",
    };
    for (size_t i = 0; i < ARRAY_SIZE(names); ++i)
        assert(MediaCodec_MatchDecoderList(patterns, names[i],
                                          strlen(names[i]), true));
    assert(!MediaCodec_MatchDecoderList(patterns, "OMX.vendor.hevc",
                                       strlen("OMX.vendor.hevc"), true));
    assert(!MediaCodec_MatchDecoderList(patterns, "OMX.dolby.decoder.hevc.extra",
                         strlen("OMX.dolby.decoder.hevc.extra"), true));
}

int main(void)
{
    TestPatterns();
    TestLists();
    TestBoundedBuffers();
    TestDolbyDefaults();
    return 0;
}
