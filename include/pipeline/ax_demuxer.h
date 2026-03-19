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
    std::string uri;
    bool realtime_playback{true};
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
