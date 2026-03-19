#pragma once

#include <cstdint>
#include <memory>

#include "common/ax_image.h"

namespace axvsdk::common {

enum class ResizeMode {
    kStretch = 0,
    kKeepAspectRatio,
};

enum class ResizeAlign {
    kCenter = 0,
    kStart,
    kEnd,
};

struct ResizeOptions {
    ResizeMode mode{ResizeMode::kStretch};
    ResizeAlign horizontal_align{ResizeAlign::kCenter};
    ResizeAlign vertical_align{ResizeAlign::kCenter};
    std::uint32_t background_color{0};
};

struct CropRect {
    std::int32_t x{0};
    std::int32_t y{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
};

struct ImageProcessRequest {
    ImageDescriptor output_image{};
    bool enable_crop{false};
    CropRect crop{};
    ResizeOptions resize{};
};

class ImageProcessor {
public:
    virtual ~ImageProcessor() = default;

    virtual AxImage::Ptr Process(const AxImage& source, const ImageProcessRequest& request) = 0;
    virtual bool Process(const AxImage& source, const ImageProcessRequest& request, AxImage& destination) = 0;
};

std::unique_ptr<ImageProcessor> CreateImageProcessor();

}  // namespace axvsdk::common
