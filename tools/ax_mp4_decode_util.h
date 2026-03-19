#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "codec/ax_video_decoder.h"
#include "pipeline/ax_demuxer.h"

namespace axvsdk::tooling {

inline codec::VideoDecoderConfig MakeDecoderConfigFromStreamInfo(const codec::VideoStreamInfo& stream_info) {
    codec::VideoDecoderConfig config{};
    config.stream = stream_info;
    return config;
}

inline std::unique_ptr<pipeline::Demuxer> OpenDemuxer(const std::string& uri,
                                                      bool realtime_playback = false,
                                                      bool loop_playback = false) {
    auto demuxer = pipeline::CreateDemuxer();
    if (!demuxer) {
        return nullptr;
    }

    pipeline::DemuxerConfig config{};
    config.uri = uri;
    config.realtime_playback = realtime_playback;
    config.loop_playback = loop_playback;
    if (!demuxer->Open(config)) {
        return nullptr;
    }
    return demuxer;
}

inline bool FeedDecoderFromDemuxer(pipeline::Demuxer& demuxer,
                                   codec::VideoDecoder& decoder,
                                   const std::atomic<bool>* stop_requested = nullptr) {
    while (stop_requested == nullptr || !stop_requested->load(std::memory_order_relaxed)) {
        codec::EncodedPacket packet;
        if (!demuxer.ReadPacket(&packet)) {
            break;
        }

        if (!decoder.SubmitPacket(std::move(packet))) {
            return false;
        }
    }

    if (stop_requested == nullptr || !stop_requested->load(std::memory_order_relaxed)) {
        return decoder.SubmitEndOfStream();
    }
    return false;
}

}  // namespace axvsdk::tooling
