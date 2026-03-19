#pragma once

#include <memory>
#include <string>
#include <vector>

#include "pipeline/ax_pipeline.h"

namespace axvsdk::pipeline {

struct ManagedPipelineInfo {
    std::string name;
    bool running{false};
    PipelineStats stats{};
};

class PipelineManager {
public:
    virtual ~PipelineManager() = default;

    virtual bool AddPipeline(const std::string& name, const PipelineConfig& config) = 0;
    virtual bool RemovePipeline(const std::string& name) = 0;
    virtual bool StartPipeline(const std::string& name) = 0;
    virtual void StopPipeline(const std::string& name) noexcept = 0;

    virtual bool HasPipeline(const std::string& name) const = 0;
    virtual Pipeline* GetPipeline(const std::string& name) = 0;
    virtual const Pipeline* GetPipeline(const std::string& name) const = 0;
    virtual bool GetPipelineInfo(const std::string& name, ManagedPipelineInfo* info) const = 0;
    virtual std::vector<ManagedPipelineInfo> ListPipelines() const = 0;

    virtual bool StartAll() = 0;
    virtual void StopAll() noexcept = 0;
    virtual void Clear() noexcept = 0;
};

std::unique_ptr<PipelineManager> CreatePipelineManager();

}  // namespace axvsdk::pipeline
