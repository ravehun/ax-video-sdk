#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "common/ax_image.h"

namespace axvsdk::codec {

struct JpegDecodeOptions {
    // 目标输出图像描述。
    // format/width/height 为空时，默认输出 JPEG 原始尺寸的 NV12 图像。
    common::ImageDescriptor output_image{};
};

struct JpegEncodeOptions {
    // JPEG 质量，常用范围 1~100。
    std::uint32_t quality{90};
};

// JPEG decode 返回的新图像默认由库内部申请。
// 板端通常为 CMM 图像；AXCL 下通常为设备侧图像。
common::AxImage::Ptr DecodeJpegFile(const std::string& path,
                                    const JpegDecodeOptions& options = {});
common::AxImage::Ptr DecodeJpegMemory(const void* data,
                                      std::size_t size,
                                      const JpegDecodeOptions& options = {});
common::AxImage::Ptr DecodeJpegBase64(const std::string& base64,
                                      const JpegDecodeOptions& options = {});

// JPEG encode 输出始终位于 host 侧。
// 若输入图像不是硬件可直接编码的格式，库会先在内部转换到合适的设备侧图像。
std::vector<std::uint8_t> EncodeJpegToMemory(const common::AxImage& image,
                                             const JpegEncodeOptions& options = {});
bool EncodeJpegToFile(const common::AxImage& image,
                      const std::string& path,
                      const JpegEncodeOptions& options = {});
std::string EncodeJpegToBase64(const common::AxImage& image,
                               const JpegEncodeOptions& options = {});

}  // namespace axvsdk::codec
