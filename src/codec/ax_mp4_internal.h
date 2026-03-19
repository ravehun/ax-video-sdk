#pragma once

#include <memory>
#include <string>

#include "codec/ax_codec_types.h"

namespace axvsdk::codec::internal {

class Mp4FileMuxer {
public:
    static std::unique_ptr<Mp4FileMuxer> Open(const std::string& file_path, const VideoStreamInfo& stream);

    Mp4FileMuxer(const Mp4FileMuxer&) = delete;
    Mp4FileMuxer& operator=(const Mp4FileMuxer&) = delete;
    Mp4FileMuxer(Mp4FileMuxer&&) noexcept;
    Mp4FileMuxer& operator=(Mp4FileMuxer&&) noexcept;
    ~Mp4FileMuxer();

    bool WritePacket(const EncodedPacket& packet);
    void Close() noexcept;

private:
    struct Impl;

    explicit Mp4FileMuxer(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace axvsdk::codec::internal
