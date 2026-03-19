#pragma once

#include <cstdint>
#include <memory>

#include "common/ax_image.h"
#include "pipeline/ax_pipeline.h"

namespace axvsdk::pipeline::internal {

class PreparedPipelineOsd {
public:
    virtual ~PreparedPipelineOsd() = default;

    virtual std::uint32_t hold_frames() const noexcept = 0;
    virtual bool Apply(common::AxImage& image) const = 0;
};

class PipelineOsdRenderer {
public:
    virtual ~PipelineOsdRenderer() = default;

    virtual std::shared_ptr<const PreparedPipelineOsd> Prepare(const PipelineOsdFrame& frame) = 0;
};

std::unique_ptr<PipelineOsdRenderer> CreatePlatformPipelineOsdRenderer();

}  // namespace axvsdk::pipeline::internal
