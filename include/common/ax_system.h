#pragma once

#include <cstdint>

namespace axvsdk::common {

// 后端选择。
// kAuto: 由编译目标自动决定。
// kAxMsp: 板端 MSP 后端。
// kAxcl: AXCL 算力卡后端。
enum class BackendType {
    kAuto = 0,
    kAxMsp,
    kAxcl,
};

// VDEC 初始化模式。
enum class VdecModule {
    kBothVideoAndJpeg = 0,
    kVideoOnly = 1,
    kJpegOnly = 2,
};

// VENC 初始化模式。
enum class VencType {
    kVideoEncoder = 1,
    kJpegEncoder = 2,
    kMultiEncoder = 3,
};

struct SystemOptions {
    // 默认按当前构建目标自动选择后端。
    BackendType backend{BackendType::kAuto};
    // 设备索引。
    // MSP 板端通常保持默认值 -1。
    // AXCL 建议显式指定，表示当前进程默认工作卡。
    std::int32_t device_id{-1};

    // 仅初始化当前进程实际需要的模块即可。
    bool enable_vdec{true};
    bool enable_venc{true};
    bool enable_ivps{true};

    // 以下参数为底层模块初始化参数，普通使用保持默认即可。
    std::uint32_t vdec_max_group_count{16};
    VdecModule vdec_module{VdecModule::kBothVideoAndJpeg};
    bool enable_vdec_mc{false};
    std::int32_t vdec_virtual_channel{0};

    VencType venc_type{VencType::kMultiEncoder};
    std::uint32_t venc_total_thread_num{3};
    bool venc_explicit_sched{false};
};

// 初始化 SDK 全局运行环境。
// 成功后才能创建 codec / pipeline / image processor 等对象。
// 重复初始化会复用现有状态，不建议不同参数重复调用。
bool InitializeSystem(const SystemOptions& options = {});
void ShutdownSystem() noexcept;
bool IsSystemInitialized() noexcept;
BackendType GetActiveBackend() noexcept;
// 返回当前全局默认 device id。
// AXCL 下用于未显式指定 device_id 的模块。
std::int32_t GetActiveDeviceId() noexcept;

}  // namespace axvsdk::common
