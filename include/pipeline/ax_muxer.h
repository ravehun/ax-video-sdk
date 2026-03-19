#pragma once

#include <memory>
#include <string>
#include <vector>

#include "codec/ax_codec_types.h"

namespace axvsdk::pipeline {

struct MuxerConfig {
    codec::VideoStreamInfo stream{};
    std::vector<std::string> uris;
};

class Muxer {
public:
    virtual ~Muxer() = default;

    virtual bool Open(const MuxerConfig& config) = 0;
    virtual void Close() noexcept = 0;
    virtual bool SubmitPacket(codec::EncodedPacket packet) = 0;
};

std::unique_ptr<Muxer> CreateMuxer();

}  // namespace axvsdk::pipeline
