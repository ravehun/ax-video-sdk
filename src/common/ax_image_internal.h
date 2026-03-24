#pragma once

#include <functional>
#include <memory>

#include "ax_global_type.h"

#include "common/ax_image.h"

namespace axvsdk::common::internal {

struct AxImageAccess {
    using FrameReleaseCallback = std::function<void(const AX_VIDEO_FRAME_INFO_T&)>;

    static AxImage::Ptr WrapVideoFrame(const AX_VIDEO_FRAME_INFO_T& frame_info,
                                       FrameReleaseCallback release_callback = {});

    static const AX_VIDEO_FRAME_INFO_T& GetAxFrameInfo(const AxImage& image) noexcept;
    static AX_VIDEO_FRAME_INFO_T* MutableAxFrameInfo(AxImage* image) noexcept;
    static const AX_VIDEO_FRAME_T& GetAxFrame(const AxImage& image) noexcept;
    static AX_VIDEO_FRAME_T* MutableAxFrame(AxImage* image) noexcept;
    static void AttachLifetime(AxImage* image, std::shared_ptr<void> lifetime) noexcept;
    // Copy timing / crop metadata but do not touch addresses/strides/block ids.
    static void CopyFrameMetadata(const AxImage& source, AxImage* destination) noexcept;
};

}  // namespace axvsdk::common::internal
