#pragma once

#include <memory>

#include "common/ax_image_processor.h"

namespace axvsdk::common::internal {

std::unique_ptr<ImageProcessor> CreatePlatformImageProcessor();

}  // namespace axvsdk::common::internal
