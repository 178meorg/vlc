/* Dolby Vision configuration in Matroska BlockAdditionMapping.
 * Copyright (C) 2026 VLC authors and VideoLAN
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef VLC_MKV_DOLBY_VISION_HPP
#define VLC_MKV_DOLBY_VISION_HPP

#include <cstddef>
#include <cstdint>

namespace mkv {

struct DolbyVisionConfig
{
    unsigned profile;
    unsigned level;
    unsigned compatibility;
    bool enhancement_layer;
};

/* BlockAddIDType is a big-endian integer, not a VLC_FOURCC value.
 * Only recognize HEVC profiles with an in-band base layer and RPU.
 * Keep CodecPrivate as hvcC: the Dolby record is not HEVC extradata. */
inline bool ParseDolbyVisionConfig(uint64_t type, const uint8_t *data,
                                  size_t size, DolbyVisionConfig &config)
{
    if ((type != 0x64766343 && type != 0x64767643) ||
        data == nullptr || size < 4 || data[0] != 1)
        return false;

    unsigned profile = data[2] >> 1;
    if (profile != 4 && profile != 5 && profile != 7 && profile != 8)
        return false;
    if ((profile < 8) != (type == 0x64766343))
        return false;
    if ((data[3] & 0x05) != 0x05) /* rpu_present_flag + bl_present_flag */
        return false;

    config.profile = profile;
    config.level = ((data[2] & 1) << 5) | (data[3] >> 3);
    config.compatibility = size >= 5 ? data[4] >> 4 : 0;
    config.enhancement_layer = (data[3] & 2) != 0;
    return true;
}

} // namespace mkv

#endif
