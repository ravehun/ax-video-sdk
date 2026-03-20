#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "codec/ax_codec_types.h"
#include "common/ax_image.h"
#include "common/ax_image_processor.h"

namespace axvsdk::codec {

// 输入队列满时的处理策略。
enum class QueueOverflowPolicy {
    // 丢弃最旧的待编码帧，优先保留最新画面。
    kDropOldest = 0,
    // 丢弃当前新送入的帧。
    kDropNewest,
    // 阻塞提交线程，直到队列有空位。
    kBlock,
};

struct VideoEncoderStats {
    std::uint64_t submitted_frames{0};
    std::uint64_t dropped_frames{0};
    std::uint64_t encoded_packets{0};
    std::uint64_t key_packets{0};
    std::size_t current_queue_depth{0};
    std::size_t queue_capacity{0};
};

struct VideoEncoderConfig {
    VideoCodecType codec{VideoCodecType::kUnknown};
    std::uint32_t width{0};
    std::uint32_t height{0};
    // 设备索引。AXCL 下建议显式指定；板端通常保持默认值即可。
    std::int32_t device_id{-1};
    // 目标输出帧率。传 0 表示内部按 30fps 或上层输入帧率估算。
    double frame_rate{0.0};
    // 目标码率，单位 kbps。传 0 表示内部根据分辨率和编码类型自动估算。
    std::uint32_t bitrate_kbps{0};
    // GOP 长度。传 0 表示内部按目标帧率自动估算。
    std::uint32_t gop{0};
    // 编码前软件输入队列深度。传 0 表示内部使用默认值。
    std::size_t input_queue_depth{0};
    QueueOverflowPolicy overflow_policy{QueueOverflowPolicy::kDropOldest};
    // 当输入图像与编码器目标尺寸不一致时，内部使用该 resize 策略做预处理。
    // 编码器硬件侧最终输入统一为 NV12。
    common::ResizeOptions resize{};
};

using PacketCallback = std::function<void(EncodedPacket packet)>;

class VideoEncoder {
public:
    virtual ~VideoEncoder() = default;

    virtual bool Open(const VideoEncoderConfig& config) = 0;
    virtual void Close() noexcept = 0;
    virtual bool Start() = 0;
    virtual void Stop() noexcept = 0;

    // 提交一帧待编码图像。
    // 调用方可传入 NV12 / RGB / BGR 等图像；若不是目标 NV12 尺寸，
    // 编码器会在内部申请或复用中间图像后再送硬件。
    // 默认情况下，中间图像使用 CMM / 设备侧内存。
    virtual bool SubmitFrame(common::AxImage::Ptr frame) = 0;
    // 获取最近一包编码输出。
    virtual bool GetLatestPacket(EncodedPacket* packet) = 0;
    virtual VideoEncoderStats GetStats() const = 0;
    // 注册码流输出回调。
    // 回调拿到的是 host 侧编码包数据，可直接用于写文件、推流或自定义处理。
    virtual void SetPacketCallback(PacketCallback callback) = 0;
};

std::unique_ptr<VideoEncoder> CreateVideoEncoder();

}  // namespace axvsdk::codec
