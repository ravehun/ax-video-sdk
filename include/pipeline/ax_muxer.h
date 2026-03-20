#pragma once

#include <memory>
#include <string>
#include <vector>

#include "codec/ax_codec_types.h"

namespace axvsdk::pipeline {

struct MuxerConfig {
    // 输出码流信息，通常由上游 encoder 配置决定。
    codec::VideoStreamInfo stream{};
    // 输出目标列表。
    // 可同时包含 MP4 文件路径和 RTSP 推流地址。
    std::vector<std::string> uris;
};

class Muxer {
public:
    virtual ~Muxer() = default;

    virtual bool Open(const MuxerConfig& config) = 0;
    virtual void Close() noexcept = 0;
    // 提交一包 host 侧编码数据。
    virtual bool SubmitPacket(codec::EncodedPacket packet) = 0;
};

std::unique_ptr<Muxer> CreateMuxer();

}  // namespace axvsdk::pipeline
