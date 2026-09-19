/* Copyright (C) 2026 VLC authors and VideoLAN
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef VLC_MEDIACODEC_PROFILE_H
#define VLC_MEDIACODEC_PROFILE_H

#include <vlc_common.h>
#include <vlc_strings.h>

/* Decoder names use glob patterns, not regular expressions. Keep the legacy
 * Dolby Vision option case-insensitive; decoder-ignore-profile is case-sensitive.
 */
static inline bool
MediaCodec_MatchDecoderPattern(const char *pattern, size_t pattern_len,
                              const char *name, size_t name_len,
                              bool ignore_case)
{
    size_t p = 0, n = 0, star = SIZE_MAX, retry = 0;

    while (n < name_len)
    {
        if (p < pattern_len && pattern[p] == '*')
        {
            star = p++;
            retry = n;
        }
        else if (p < pattern_len &&
                 (pattern[p] == '?' || pattern[p] == name[n] ||
                  (ignore_case && vlc_ascii_tolower(pattern[p]) ==
                                  vlc_ascii_tolower(name[n]))))
        {
            p++;
            n++;
        }
        else if (star != SIZE_MAX)
        {
            p = star + 1;
            n = ++retry;
        }
        else
            return false;
    }
    while (p < pattern_len && pattern[p] == '*')
        p++;
    return p == pattern_len;
}

static inline bool
MediaCodec_MatchDecoderList(const char *patterns, const char *name,
                           size_t name_len, bool ignore_case)
{
    if (patterns == NULL)
        return false;

    for (const char *pattern = patterns; pattern != NULL; )
    {
        const char *next = strchr(pattern, ',');
        size_t len = next != NULL ? (size_t)(next - pattern) : strlen(pattern);

        while (len > 0 && (*pattern == ' ' || *pattern == '\t'))
        {
            pattern++;
            len--;
        }
        while (len > 0 && (pattern[len - 1] == ' ' ||
                           pattern[len - 1] == '\t'))
            len--;

        if (len > 0 && MediaCodec_MatchDecoderPattern(pattern, len, name,
                                                     name_len, ignore_case))
            return true;
        pattern = next != NULL ? next + 1 : NULL;
    }
    return false;
}

#endif /* VLC_MEDIACODEC_PROFILE_H */
