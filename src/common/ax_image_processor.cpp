#include "common/ax_image_processor.h"

#include "ax_image_processor_internal.h"

namespace axvsdk::common {

std::unique_ptr<ImageProcessor> CreateImageProcessor() {
    return internal::CreatePlatformImageProcessor();
}

}  // namespace axvsdk::common
