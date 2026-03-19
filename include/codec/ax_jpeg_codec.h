#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "common/ax_image.h"

namespace axvsdk::codec {

struct JpegDecodeOptions {
    common::ImageDescriptor output_image{};
};

struct JpegEncodeOptions {
    std::uint32_t quality{90};
};

common::AxImage::Ptr DecodeJpegFile(const std::string& path,
                                    const JpegDecodeOptions& options = {});
common::AxImage::Ptr DecodeJpegMemory(const void* data,
                                      std::size_t size,
                                      const JpegDecodeOptions& options = {});
common::AxImage::Ptr DecodeJpegBase64(const std::string& base64,
                                      const JpegDecodeOptions& options = {});

std::vector<std::uint8_t> EncodeJpegToMemory(const common::AxImage& image,
                                             const JpegEncodeOptions& options = {});
bool EncodeJpegToFile(const common::AxImage& image,
                      const std::string& path,
                      const JpegEncodeOptions& options = {});
std::string EncodeJpegToBase64(const common::AxImage& image,
                               const JpegEncodeOptions& options = {});

}  // namespace axvsdk::codec
