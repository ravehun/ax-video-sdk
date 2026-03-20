#pragma once

#include <cstdint>
#include <memory>

#include "common/ax_image.h"

namespace axvsdk::common {

// resize 模式。
// kStretch: 直接拉伸到目标尺寸。
// kKeepAspectRatio: 保持原始宽高比，多余区域用 background_color 填充。
enum class ResizeMode {
    kStretch = 0,
    kKeepAspectRatio,
};

// 当 KeepAspectRatio 产生留边时，控制图像在目标画面中的对齐方式。
enum class ResizeAlign {
    kCenter = 0,
    kStart,
    kEnd,
};

struct ResizeOptions {
    ResizeMode mode{ResizeMode::kStretch};
    ResizeAlign horizontal_align{ResizeAlign::kCenter};
    ResizeAlign vertical_align{ResizeAlign::kCenter};
    // 留边填充值，当前按 0xRRGGBB 传入。
    // NV12 输出时由底层转换成对应 YUV 背景色。
    std::uint32_t background_color{0};
};

struct CropRect {
    std::int32_t x{0};
    std::int32_t y{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
};

struct ImageProcessRequest {
    // 目标图像描述。
    // 未填写的 format/width/height 可由调用方按具体 API 语义补全。
    ImageDescriptor output_image{};
    bool enable_crop{false};
    CropRect crop{};
    ResizeOptions resize{};
};

class ImageProcessor {
public:
    virtual ~ImageProcessor() = default;

    // 返回值版本：
    // 由库内部新申请一张输出图像并返回。
    // 默认适合更重视易用性的场景。
    virtual AxImage::Ptr Process(const AxImage& source, const ImageProcessRequest& request) = 0;
    // 出参版本：
    // 调用方自己准备 destination，库只负责写入内容。
    // 更适合需要复用目标缓冲、减少重复分配的场景。
    virtual bool Process(const AxImage& source, const ImageProcessRequest& request, AxImage& destination) = 0;
};

std::unique_ptr<ImageProcessor> CreateImageProcessor();

}  // namespace axvsdk::common
