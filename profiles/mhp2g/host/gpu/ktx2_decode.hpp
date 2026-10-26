#pragma once

#include "basisu_transcoder.h"
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace mhp2g::gpu {

inline bool is_ktx2(const std::vector<std::uint8_t> &bytes) {
    static constexpr unsigned char id[] = {0xab,0x4b,0x54,0x58,0x20,0x32,0x30,0xbb,0x0d,0x0a,0x1a,0x0a};
    return bytes.size() >= sizeof(id) && std::memcmp(bytes.data(),id,sizeof(id)) == 0;
}

// Basis Universal performs ETC1S/UASTC transcoding, including Zstandard
// supercompression. Return the first 2D level as RGBA for the existing GPU
// upload path; no driver-specific compressed-format support is required.
inline bool decode_ktx2(const std::vector<std::uint8_t> &bytes, std::vector<std::uint8_t> &pixels,
    int &width, int &height, std::string &error) {
    auto reject = [&](const char *why) { error=why;return false; };
    if (!is_ktx2(bytes) || bytes.size()<104 || bytes.size()>256ull*1024*1024)
        return reject("invalid or oversized KTX2 file");
    const auto u32 = [&](std::size_t at) {
        return static_cast<std::uint32_t>(bytes[at]) | static_cast<std::uint32_t>(bytes[at+1])<<8 |
            static_cast<std::uint32_t>(bytes[at+2])<<16 | static_cast<std::uint32_t>(bytes[at+3])<<24;
    };
    const auto u64 = [&](std::size_t at) {return static_cast<std::uint64_t>(u32(at)) |
        static_cast<std::uint64_t>(u32(at+4))<<32;};
    const std::uint32_t w=u32(20),h=u32(24),levels=u32(40);
    if (!w || !h || w>8192 || h>8192 || u32(28)>1 || u32(32)>1 || u32(36)!=1 || !levels || levels>14)
        return reject("KTX2 must be a 2D texture no larger than 8192 pixels per side");
    if (bytes.size()<80ull+24ull*levels) return reject("truncated KTX2 level index");
    for (std::uint32_t level=0;level<levels;++level) {
        const std::size_t at=80+24*level;
        const std::uint64_t offset=u64(at),size=u64(at+8),decoded=u64(at+16);
        if (offset>bytes.size() || size>bytes.size()-offset || decoded>256ull*1024*1024)
            return reject("invalid or oversized KTX2 level");
    }
    static std::once_flag initialized;
    std::call_once(initialized, [] {basist::basisu_transcoder_init();});
    basist::ktx2_transcoder decoder;
    if (!decoder.init(bytes.data(),static_cast<std::uint32_t>(bytes.size())) || !decoder.start_transcoding())
        return reject("unsupported or damaged KTX2 Basis texture");
    if (decoder.get_width()!=w || decoder.get_height()!=h)
        return reject("inconsistent KTX2 dimensions");
    pixels.resize(static_cast<std::size_t>(w)*h*4);
    if (!decoder.transcode_image_level(0,0,0,pixels.data(),w*h,basist::transcoder_texture_format::cTFRGBA32)) {
        pixels.clear();
        return reject("KTX2 transcoding failed");
    }
    width=static_cast<int>(w);height=static_cast<int>(h);
    return true;
}

} // namespace mhp2g::gpu
