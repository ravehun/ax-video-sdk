#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <rtsp-client/rtsp_client.h>
#include <rtsp-common/common.h>

#include "codec/ax_codec_types.h"

namespace axvsdk::pipeline::internal {

struct RtspUrlTarget {
    std::string host{"0.0.0.0"};
    std::uint16_t port{8554};
    std::string path{"/live"};
};

struct AnnexBNalu {
    const std::uint8_t* data{nullptr};
    std::size_t size{0};
    std::uint8_t type{0};
};

inline codec::VideoCodecType ToSdkCodec(rtsp::CodecType codec) noexcept {
    return codec == rtsp::CodecType::H265 ? codec::VideoCodecType::kH265 : codec::VideoCodecType::kH264;
}

inline rtsp::CodecType ToRtspCodec(codec::VideoCodecType codec) noexcept {
    return codec == codec::VideoCodecType::kH265 ? rtsp::CodecType::H265 : rtsp::CodecType::H264;
}

inline bool IsRtspUrl(const std::string& uri) noexcept {
    return uri.rfind("rtsp://", 0) == 0 || uri.rfind("rtsps://", 0) == 0;
}

inline bool ParseRtspUrl(const std::string& url, RtspUrlTarget* target) {
    if (target == nullptr || !IsRtspUrl(url)) {
        return false;
    }

    const auto scheme_end = url.find("://");
    const auto remainder = url.substr(scheme_end + 3U);
    const auto slash = remainder.find('/');
    const auto host_port = slash == std::string::npos ? remainder : remainder.substr(0, slash);
    target->path = slash == std::string::npos ? "/live" : remainder.substr(slash);
    if (target->path.empty()) {
        target->path = "/live";
    }

    const auto colon = host_port.rfind(':');
    if (colon == std::string::npos) {
        target->host = host_port;
        target->port = 554;
        return true;
    }

    target->host = host_port.substr(0, colon);
    target->port = static_cast<std::uint16_t>(std::stoi(host_port.substr(colon + 1)));
    return true;
}

inline std::string MakePublisherUrl(const RtspUrlTarget& target) {
    const std::string host = (target.host.empty() || target.host == "0.0.0.0" || target.host == "::")
                                 ? "127.0.0.1"
                                 : target.host;
    return "rtsp://" + host + ":" + std::to_string(target.port) + target.path;
}

inline bool IsStartCode3(const std::uint8_t* data) noexcept {
    return data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01;
}

inline bool IsStartCode4(const std::uint8_t* data) noexcept {
    return data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01;
}

inline std::vector<AnnexBNalu> ParseAnnexBNalus(const std::vector<std::uint8_t>& data,
                                                codec::VideoCodecType codec) {
    std::vector<AnnexBNalu> nalus;
    const auto* bytes = data.data();
    const auto size = data.size();
    std::size_t offset = 0;

    auto find_start = [&](std::size_t begin) -> std::pair<std::size_t, std::size_t> {
        for (std::size_t index = begin; index + 3 <= size; ++index) {
            if (index + 4 <= size && IsStartCode4(bytes + index)) {
                return {index, 4};
            }
            if (index + 3 <= size && IsStartCode3(bytes + index)) {
                return {index, 3};
            }
        }
        return {size, 0};
    };

    while (true) {
        const auto [start, prefix] = find_start(offset);
        if (prefix == 0) {
            break;
        }

        const std::size_t payload_start = start + prefix;
        auto next = find_start(payload_start);
        std::size_t next_start = next.first;
        if (next_start == size) {
            next_start = size;
        }
        if (payload_start >= next_start) {
            offset = payload_start;
            continue;
        }

        const auto* nalu = bytes + payload_start;
        const auto nalu_size = next_start - payload_start;
        std::uint8_t type = 0;
        if (codec == codec::VideoCodecType::kH264) {
            type = static_cast<std::uint8_t>(nalu[0] & 0x1FU);
        } else if (codec == codec::VideoCodecType::kH265 && nalu_size >= 2) {
            type = static_cast<std::uint8_t>((nalu[0] >> 1U) & 0x3FU);
        }

        nalus.push_back({nalu, nalu_size, type});
        offset = next_start;
    }

    return nalus;
}

inline void AppendAnnexBNalu(const std::vector<std::uint8_t>& nalu, std::vector<std::uint8_t>* output) {
    if (output == nullptr || nalu.empty()) {
        return;
    }

    static constexpr std::uint8_t kStartCode[4] = {0x00, 0x00, 0x00, 0x01};
    output->insert(output->end(), std::begin(kStartCode), std::end(kStartCode));
    output->insert(output->end(), nalu.begin(), nalu.end());
}

inline void UpdateCodecConfig(codec::VideoCodecType codec,
                              const std::vector<std::uint8_t>& annexb,
                              std::vector<std::uint8_t>* vps,
                              std::vector<std::uint8_t>* sps,
                              std::vector<std::uint8_t>* pps) {
    if (sps == nullptr || pps == nullptr || (codec == codec::VideoCodecType::kH265 && vps == nullptr)) {
        return;
    }

    for (const auto& nalu : ParseAnnexBNalus(annexb, codec)) {
        if (codec == codec::VideoCodecType::kH264) {
            if (nalu.type == 7) {
                sps->assign(nalu.data, nalu.data + nalu.size);
            } else if (nalu.type == 8) {
                pps->assign(nalu.data, nalu.data + nalu.size);
            }
        } else if (codec == codec::VideoCodecType::kH265) {
            if (nalu.type == 32) {
                vps->assign(nalu.data, nalu.data + nalu.size);
            } else if (nalu.type == 33) {
                sps->assign(nalu.data, nalu.data + nalu.size);
            } else if (nalu.type == 34) {
                pps->assign(nalu.data, nalu.data + nalu.size);
            }
        }
    }
}

inline bool HasCodecConfig(codec::VideoCodecType codec,
                           const std::vector<std::uint8_t>& vps,
                           const std::vector<std::uint8_t>& sps,
                           const std::vector<std::uint8_t>& pps) {
    if (codec == codec::VideoCodecType::kH264) {
        return !sps.empty() && !pps.empty();
    }
    if (codec == codec::VideoCodecType::kH265) {
        return !vps.empty() && !sps.empty() && !pps.empty();
    }
    return false;
}

inline std::vector<std::uint8_t> BuildDecoderConfigPrefix(const rtsp::MediaInfo& media,
                                                          codec::VideoCodecType codec) {
    std::vector<std::uint8_t> prefix;
    if (codec == codec::VideoCodecType::kH265) {
        AppendAnnexBNalu(media.vps, &prefix);
    }
    AppendAnnexBNalu(media.sps, &prefix);
    AppendAnnexBNalu(media.pps, &prefix);
    return prefix;
}

}  // namespace axvsdk::pipeline::internal
