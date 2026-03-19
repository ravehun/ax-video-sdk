#pragma once

#include <cstdint>
#include <vector>

namespace axvsdk::codec {

enum class VideoCodecType {
    kUnknown = 0,
    kH264,
    kH265,
    kJpeg,
};

struct EncodedPacket {
    VideoCodecType codec{VideoCodecType::kUnknown};
    std::vector<std::uint8_t> data;
    std::uint64_t pts{0};
    std::uint64_t duration{0};
    bool key_frame{false};
};

struct VideoStreamInfo {
    VideoCodecType codec{VideoCodecType::kUnknown};
    std::uint32_t width{0};
    std::uint32_t height{0};
    double frame_rate{0.0};
};

struct Mp4VideoInfo {
    VideoCodecType codec{VideoCodecType::kUnknown};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t timescale{0};
    double fps{0.0};
    std::uint32_t sample_count{0};
};

}  // namespace axvsdk::codec
