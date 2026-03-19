#pragma once

#include "ax_global_type.h"

#include "common/ax_image.h"

namespace axvsdk::common::internal {

bool CopyImage(const AxImage& source, AxImage* destination) noexcept;
bool CopyVideoFrameToImage(const AX_VIDEO_FRAME_INFO_T& frame_info, AxImage* destination) noexcept;

}  // namespace axvsdk::common::internal
