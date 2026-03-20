#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "codec/ax_codec_types.h"

namespace axvsdk::pipeline {

enum class DemuxerInputType {
    kUnknown = 0,
    kMp4File,
    kRtspPull,
};

struct DemuxerConfig {
    // 输入 URI。
    // 当前主要支持 MP4 文件路径和 RTSP 拉流地址。
    std::string uri;
    // MP4 输入时：
    // true 表示按源视频帧率节奏送包，适合模拟实时源；
    // false 表示尽快读取，适合离线转码。
    // RTSP 输入下通常按实时流行为工作。
    bool realtime_playback{true};
    // 仅对可复位文件型输入生效，例如 MP4。
    bool loop_playback{false};
};

class Demuxer {
public:
    virtual ~Demuxer() = default;

    virtual bool Open(const DemuxerConfig& config) = 0;
    virtual void Close() noexcept = 0;

    virtual bool ReadPacket(codec::EncodedPacket* packet) = 0;
    virtual bool Reset() noexcept = 0;
    virtual void Interrupt() noexcept = 0;
    virtual codec::VideoStreamInfo GetVideoStreamInfo() const noexcept = 0;
};

bool DetectDemuxerInputType(const std::string& uri, DemuxerInputType* type) noexcept;

std::unique_ptr<Demuxer> CreateDemuxer();

}  // namespace axvsdk::pipeline
