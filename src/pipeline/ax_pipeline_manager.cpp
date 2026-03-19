#include "pipeline/ax_pipeline_manager.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace axvsdk::pipeline {

namespace {

class AxPipelineManager final : public PipelineManager {
public:
    ~AxPipelineManager() override {
        Clear();
    }

    bool AddPipeline(const std::string& name, const PipelineConfig& config) override {
        if (name.empty()) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (pipelines_.find(name) != pipelines_.end()) {
            return false;
        }

        auto pipeline = CreatePipeline();
        if (!pipeline || !pipeline->Open(config)) {
            return false;
        }

        pipelines_.emplace(name, PipelineEntry{std::move(pipeline), false});
        return true;
    }

    bool RemovePipeline(const std::string& name) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pipelines_.find(name);
        if (it == pipelines_.end()) {
            return false;
        }

        CloseEntry(it->second);
        pipelines_.erase(it);
        return true;
    }

    bool StartPipeline(const std::string& name) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pipelines_.find(name);
        if (it == pipelines_.end()) {
            return false;
        }

        if (it->second.running) {
            return true;
        }

        if (!it->second.pipeline->Start()) {
            return false;
        }

        it->second.running = true;
        return true;
    }

    void StopPipeline(const std::string& name) noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pipelines_.find(name);
        if (it == pipelines_.end()) {
            return;
        }

        StopEntry(it->second);
    }

    bool HasPipeline(const std::string& name) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return pipelines_.find(name) != pipelines_.end();
    }

    Pipeline* GetPipeline(const std::string& name) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = pipelines_.find(name);
        return it == pipelines_.end() ? nullptr : it->second.pipeline.get();
    }

    const Pipeline* GetPipeline(const std::string& name) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = pipelines_.find(name);
        return it == pipelines_.end() ? nullptr : it->second.pipeline.get();
    }

    bool GetPipelineInfo(const std::string& name, ManagedPipelineInfo* info) const override {
        if (info == nullptr) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = pipelines_.find(name);
        if (it == pipelines_.end()) {
            return false;
        }

        info->name = name;
        info->running = it->second.running;
        info->stats = it->second.pipeline->GetStats();
        return true;
    }

    std::vector<ManagedPipelineInfo> ListPipelines() const override {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<ManagedPipelineInfo> infos;
        infos.reserve(pipelines_.size());
        for (const auto& [name, entry] : pipelines_) {
            infos.push_back(ManagedPipelineInfo{
                name,
                entry.running,
                entry.pipeline->GetStats(),
            });
        }

        std::sort(infos.begin(), infos.end(), [](const ManagedPipelineInfo& lhs, const ManagedPipelineInfo& rhs) {
            return lhs.name < rhs.name;
        });
        return infos;
    }

    bool StartAll() override {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<PipelineEntry*> started_entries;
        started_entries.reserve(pipelines_.size());
        for (auto& [name, entry] : pipelines_) {
            (void)name;
            if (entry.running) {
                continue;
            }

            if (!entry.pipeline->Start()) {
                for (auto* started_entry : started_entries) {
                    started_entry->pipeline->Stop();
                    started_entry->running = false;
                }
                return false;
            }

            entry.running = true;
            started_entries.push_back(&entry);
        }

        return true;
    }

    void StopAll() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [name, entry] : pipelines_) {
            (void)name;
            StopEntry(entry);
        }
    }

    void Clear() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [name, entry] : pipelines_) {
            (void)name;
            CloseEntry(entry);
        }
        pipelines_.clear();
    }

private:
    struct PipelineEntry {
        std::unique_ptr<Pipeline> pipeline;
        bool running{false};
    };

    static void StopEntry(PipelineEntry& entry) noexcept {
        if (!entry.running) {
            return;
        }

        entry.pipeline->Stop();
        entry.running = false;
    }

    static void CloseEntry(PipelineEntry& entry) noexcept {
        StopEntry(entry);
        entry.pipeline->Close();
    }

    mutable std::mutex mutex_;
    std::unordered_map<std::string, PipelineEntry> pipelines_;
};

}  // namespace

std::unique_ptr<PipelineManager> CreatePipelineManager() {
    return std::make_unique<AxPipelineManager>();
}

}  // namespace axvsdk::pipeline
