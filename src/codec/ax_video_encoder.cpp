#include "ax_video_encoder_internal.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "common/ax_system.h"

namespace axvsdk::codec {

std::unique_ptr<VideoEncoder> CreateVideoEncoder() {
    return internal::CreatePlatformVideoEncoder();
}

}  // namespace axvsdk::codec

namespace axvsdk::codec::internal {

namespace {

constexpr int kEncoderStopIdlePollLimit = 10;

std::uint32_t EstimateBitrateKbps(VideoCodecType codec,
                                  std::uint32_t width,
                                  std::uint32_t height,
                                  double frame_rate) noexcept {
    if (width == 0 || height == 0) {
        return 2048;
    }

    const double effective_fps = frame_rate > 0.0 ? frame_rate : 30.0;
    const double bits_per_pixel = codec == VideoCodecType::kH265 ? 0.045 : 0.070;
    const double bits_per_second =
        static_cast<double>(width) * static_cast<double>(height) * effective_fps * bits_per_pixel;
    const auto estimated_kbps = static_cast<std::uint32_t>(bits_per_second / 1000.0);
    return std::max<std::uint32_t>(256, estimated_kbps);
}

ResolvedVideoEncoderConfig ResolveConfig(const VideoEncoderConfig& config) noexcept {
    ResolvedVideoEncoderConfig resolved{};
    resolved.codec = config.codec;
    resolved.width = config.width;
    resolved.height = config.height;
    resolved.max_width = config.width;
    resolved.max_height = config.height;
    resolved.src_frame_rate = config.frame_rate > 0.0 ? config.frame_rate : 30.0;
    resolved.dst_frame_rate = resolved.src_frame_rate;
    resolved.bitrate_kbps = config.bitrate_kbps > 0
                                ? config.bitrate_kbps
                                : EstimateBitrateKbps(config.codec, config.width, config.height, resolved.dst_frame_rate);
    resolved.gop = config.gop > 0 ? config.gop
                                  : static_cast<std::uint32_t>(std::max(1.0, resolved.dst_frame_rate));
    resolved.input_queue_depth = config.input_queue_depth > 0 ? config.input_queue_depth : 10;
    resolved.overflow_policy = config.overflow_policy;
    resolved.stream_buffer_size = std::max<std::size_t>(
        static_cast<std::size_t>(config.width) * static_cast<std::size_t>(config.height), 1024U * 1024U);
    return resolved;
}

}  // namespace

AxVideoEncoderBase::~AxVideoEncoderBase() = default;

bool AxVideoEncoderBase::Open(const VideoEncoderConfig& config) {
    if (open_) {
        Close();
    }

    if (!common::IsSystemInitialized()) {
        return false;
    }

    if ((config.codec != VideoCodecType::kH264 && config.codec != VideoCodecType::kH265) || config.width == 0 ||
        config.height == 0) {
        return false;
    }

    config_ = ResolveConfig(config);
    if (!CreateBackend(config_)) {
        return false;
    }

    open_ = true;
    return true;
}

void AxVideoEncoderBase::Close() noexcept {
    Stop();
    if (open_) {
        DestroyBackend();
        {
            std::lock_guard<std::mutex> lock(input_mutex_);
            pending_frames_.clear();
        }
        {
            std::lock_guard<std::mutex> lock(packet_mutex_);
            latest_packet_ = {};
            has_latest_packet_ = false;
        }
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            packet_callback_ = {};
        }
        config_ = {};
        submitted_frames_.store(0, std::memory_order_relaxed);
        dropped_frames_.store(0, std::memory_order_relaxed);
        encoded_packets_.store(0, std::memory_order_relaxed);
        key_packets_.store(0, std::memory_order_relaxed);
        open_ = false;
    }
}

bool AxVideoEncoderBase::Start() {
    if (!open_ || running_) {
        return false;
    }

    stop_requested_ = false;
    if (!StartBackend()) {
        return false;
    }

    send_thread_ = std::thread(&AxVideoEncoderBase::SendLoop, this);
    stream_thread_ = std::thread(&AxVideoEncoderBase::StreamLoop, this);
    running_ = true;
    return true;
}

void AxVideoEncoderBase::Stop() noexcept {
    if (!running_) {
        return;
    }

    stop_requested_ = true;
    input_cv_.notify_all();

    if (send_thread_.joinable()) {
        send_thread_.join();
    }

    StopBackend();

    if (stream_thread_.joinable()) {
        stream_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        pending_frames_.clear();
    }
    input_cv_.notify_all();

    running_ = false;
}

bool AxVideoEncoderBase::SubmitFrame(common::AxImage::Ptr frame) {
    if (!running_ || !frame || !ValidateInputFrame(*frame)) {
        return false;
    }

    std::unique_lock<std::mutex> lock(input_mutex_);
    while (pending_frames_.size() >= config_.input_queue_depth) {
        if (config_.overflow_policy == QueueOverflowPolicy::kDropNewest) {
            dropped_frames_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        if (config_.overflow_policy == QueueOverflowPolicy::kBlock) {
            input_cv_.wait(lock, [this] {
                return stop_requested_ || pending_frames_.size() < config_.input_queue_depth;
            });
            if (stop_requested_) {
                return false;
            }
            continue;
        }

        pending_frames_.pop_front();
        dropped_frames_.fetch_add(1, std::memory_order_relaxed);
    }
    pending_frames_.push_back(std::move(frame));
    submitted_frames_.fetch_add(1, std::memory_order_relaxed);
    input_cv_.notify_one();
    return true;
}

bool AxVideoEncoderBase::GetLatestPacket(EncodedPacket* packet) {
    if (packet == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(packet_mutex_);
    if (!has_latest_packet_) {
        return false;
    }

    *packet = latest_packet_;
    return true;
}

VideoEncoderStats AxVideoEncoderBase::GetStats() const {
    VideoEncoderStats stats{};
    stats.submitted_frames = submitted_frames_.load(std::memory_order_relaxed);
    stats.dropped_frames = dropped_frames_.load(std::memory_order_relaxed);
    stats.encoded_packets = encoded_packets_.load(std::memory_order_relaxed);
    stats.key_packets = key_packets_.load(std::memory_order_relaxed);
    stats.queue_capacity = config_.input_queue_depth;
    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        stats.current_queue_depth = pending_frames_.size();
    }
    return stats;
}

void AxVideoEncoderBase::SetPacketCallback(PacketCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    packet_callback_ = std::move(callback);
}

const ResolvedVideoEncoderConfig& AxVideoEncoderBase::config() const noexcept {
    return config_;
}

bool AxVideoEncoderBase::ValidateInputFrame(const common::AxImage& frame) const noexcept {
    return frame.format() == common::PixelFormat::kNv12 && frame.width() == config_.width &&
           frame.height() == config_.height;
}

void AxVideoEncoderBase::SendLoop() {
    while (true) {
        common::AxImage::Ptr frame;
        {
            std::unique_lock<std::mutex> lock(input_mutex_);
            input_cv_.wait(lock, [this] { return stop_requested_ || !pending_frames_.empty(); });
            if (stop_requested_ && pending_frames_.empty()) {
                return;
            }

            frame = std::move(pending_frames_.front());
            pending_frames_.pop_front();
            input_cv_.notify_all();
        }

        if (!frame || !ValidateInputFrame(*frame)) {
            continue;
        }

        (void)frame->FlushCache();
        (void)SendFrameToEncoder(*frame);
    }
}

void AxVideoEncoderBase::StreamLoop() {
    int idle_polls_after_stop = 0;
    while (true) {
        EncodedPacket packet;
        bool flow_end = false;
        if (!ReceivePacketFromEncoder(&packet, &flow_end)) {
            if (flow_end) {
                return;
            }
            if (stop_requested_ && ++idle_polls_after_stop >= kEncoderStopIdlePollLimit) {
                return;
            }
            continue;
        }

        idle_polls_after_stop = 0;
        encoded_packets_.fetch_add(1, std::memory_order_relaxed);
        if (packet.key_frame) {
            key_packets_.fetch_add(1, std::memory_order_relaxed);
        }

        {
            std::lock_guard<std::mutex> lock(packet_mutex_);
            latest_packet_ = packet;
            has_latest_packet_ = true;
        }

        PacketCallback callback;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callback = packet_callback_;
        }
        if (callback) {
            callback(packet);
        }
    }
}

}  // namespace axvsdk::codec::internal
