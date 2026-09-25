/* Copyright (C) 2026 VLC authors and VideoLAN
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#undef NDEBUG
#include <cassert>
#include "../../../modules/demux/mkv/dolby_vision.hpp"

int main()
{
    mkv::DolbyVisionConfig config;
    const uint64_t dvcc = 0x64766343, dvvc = 0x64767643;
    const uint8_t p4[] = { 1, 0, 0x08, 0x37, 0 };
    const uint8_t p5[] = { 1, 0, 0x0a, 0x35, 0 };
    const uint8_t p7[] = { 1, 0, 0x0e, 0x37, 0x10 };
    const uint8_t p8[] = { 1, 0, 0x10, 0x35, 0x10 };
    assert(mkv::ParseDolbyVisionConfig(dvcc, p4, sizeof(p4), config));
    assert(config.profile == 4 && config.enhancement_layer);
    assert(mkv::ParseDolbyVisionConfig(dvcc, p5, sizeof(p5), config));
    assert(config.profile == 5 && config.level == 6 &&
           config.compatibility == 0 && !config.enhancement_layer);
    assert(mkv::ParseDolbyVisionConfig(dvcc, p7, sizeof(p7), config));
    assert(config.profile == 7 && config.enhancement_layer);
    assert(mkv::ParseDolbyVisionConfig(dvvc, p8, sizeof(p8), config));
    assert(config.profile == 8 && config.compatibility == 1);
    assert(mkv::ParseDolbyVisionConfig(dvcc, p5, 4, config));
    assert(config.compatibility == 0);
    for (size_t n = 0; n < 4; ++n)
        assert(!mkv::ParseDolbyVisionConfig(dvcc, p5, n, config));
    assert(!mkv::ParseDolbyVisionConfig(dvcc, nullptr, 24, config));
    assert(!mkv::ParseDolbyVisionConfig(0x43766364, p5, 5, config));
    assert(!mkv::ParseDolbyVisionConfig(dvvc, p5, 5, config));
    assert(!mkv::ParseDolbyVisionConfig(dvcc, p8, 5, config));
    const uint8_t high_level[] = { 1, 0, 0x11, 0xfd, 0xf0 };
    assert(mkv::ParseDolbyVisionConfig(dvvc, high_level,
                                      sizeof(high_level), config));
    assert(config.profile == 8 && config.level == 63 &&
           config.compatibility == 15 && !config.enhancement_layer);
    uint8_t invalid[] = { 1, 0, 0x0a, 0x35, 0 };
    invalid[3] &= ~4; // no RPU
    assert(!mkv::ParseDolbyVisionConfig(dvcc, invalid, 5, config));
    invalid[3] = 0x34; // no base layer
    assert(!mkv::ParseDolbyVisionConfig(dvcc, invalid, 5, config));
    invalid[3] = 0x35;
    invalid[2] = 18; // AVC profile 9
    assert(!mkv::ParseDolbyVisionConfig(dvvc, invalid, 5, config));
    invalid[2] = 0x0a;
    invalid[0] = 2; // unknown configuration version
    assert(!mkv::ParseDolbyVisionConfig(dvcc, invalid, 5, config));
}
