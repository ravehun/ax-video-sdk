#include "common/ax_system.h"

#include <cstddef>
#include <cstring>
#include <iostream>
#include <mutex>

#include "ax_ivps_api.h"
#include "ax_sys_api.h"
#include "ax_vdec_api.h"
#include "ax_vdec_type.h"
#include "ax_venc_api.h"
#include "ax_venc_comm.h"

namespace axvsdk::common {

namespace {

#if defined(AXSDK_CHIP_AX650)
AX_VDEC_ENABLE_MOD_E ToAxVdecModule(VdecModule module) noexcept {
    switch (module) {
    case VdecModule::kVideoOnly:
        return AX_ENABLE_ONLY_VDEC;
    case VdecModule::kJpegOnly:
        return AX_ENABLE_ONLY_JDEC;
    case VdecModule::kBothVideoAndJpeg:
    default:
        return AX_ENABLE_BOTH_VDEC_JDEC;
    }
}
#endif

AX_VENC_ENCODER_TYPE_E ToAxVencType(VencType type) noexcept {
    switch (type) {
    case VencType::kVideoEncoder:
        return AX_VENC_VIDEO_ENCODER;
    case VencType::kJpegEncoder:
        return AX_VENC_JPEG_ENCODER;
    case VencType::kMultiEncoder:
    default:
        return AX_VENC_MULTI_ENCODER;
    }
}

struct SystemState {
    bool sys_initialized{false};
    bool vdec_initialized{false};
    bool venc_initialized{false};
    bool ivps_initialized{false};
    std::size_t ref_count{0};
};

SystemState& GetSystemState() {
    static SystemState state;
    return state;
}

std::mutex& GetSystemMutex() {
    static std::mutex mutex;
    return mutex;
}

AX_VDEC_MOD_ATTR_T MakeVdecModAttr(const SystemOptions& options) {
    AX_VDEC_MOD_ATTR_T mod_attr{};
    mod_attr.u32MaxGroupCount = options.vdec_max_group_count;
#if defined(AXSDK_CHIP_AX650)
    mod_attr.enDecModule = ToAxVdecModule(options.vdec_module);
    mod_attr.bVdecMc = options.enable_vdec_mc ? AX_TRUE : AX_FALSE;
    mod_attr.VdecVirtChn = static_cast<AX_VDEC_CHN>(options.vdec_virtual_channel);
#endif
    return mod_attr;
}

AX_VENC_MOD_ATTR_T MakeVencModAttr(const SystemOptions& options) {
    AX_VENC_MOD_ATTR_T mod_attr{};
    mod_attr.enVencType = ToAxVencType(options.venc_type);
    mod_attr.stModThdAttr.u32TotalThreadNum = options.venc_total_thread_num;
    mod_attr.stModThdAttr.bExplicitSched = options.venc_explicit_sched ? AX_TRUE : AX_FALSE;
    return mod_attr;
}

}  // namespace

bool InitializeSystem(const SystemOptions& options) {
    std::lock_guard<std::mutex> lock(GetSystemMutex());
    auto& state = GetSystemState();

    bool did_sys = false;
    bool did_vdec = false;
    bool did_venc = false;
    bool did_ivps = false;

    if (!state.sys_initialized) {
        const auto ret = AX_SYS_Init();
        if (ret != AX_SUCCESS) {
            std::cerr << "AX_SYS_Init failed, ret=0x" << std::hex << ret << std::dec << "\n";
            return false;
        }
        state.sys_initialized = true;
        did_sys = true;
    }

    if (options.enable_vdec && !state.vdec_initialized) {
        const auto mod_attr = MakeVdecModAttr(options);
        const auto ret = AX_VDEC_Init(&mod_attr);
        if (ret != AX_SUCCESS) {
            std::cerr << "AX_VDEC_Init failed, ret=0x" << std::hex << ret << std::dec << "\n";
            goto rollback;
        }
        state.vdec_initialized = true;
        did_vdec = true;
    }

    if (options.enable_venc && !state.venc_initialized) {
        const auto mod_attr = MakeVencModAttr(options);
        const auto ret = AX_VENC_Init(&mod_attr);
        if (ret != AX_SUCCESS) {
            std::cerr << "AX_VENC_Init failed, ret=0x" << std::hex << ret << std::dec << "\n";
            goto rollback;
        }
        state.venc_initialized = true;
        did_venc = true;
    }

    if (options.enable_ivps && !state.ivps_initialized) {
        const auto ret = AX_IVPS_Init();
        if (ret != AX_SUCCESS) {
            std::cerr << "AX_IVPS_Init failed, ret=0x" << std::hex << ret << std::dec << "\n";
            goto rollback;
        }
        state.ivps_initialized = true;
        did_ivps = true;
    }

    ++state.ref_count;
    return true;

rollback:
    if (did_ivps) {
        (void)AX_IVPS_Deinit();
        state.ivps_initialized = false;
    }
    if (did_venc) {
        (void)AX_VENC_Deinit();
        state.venc_initialized = false;
    }
    if (did_vdec) {
        (void)AX_VDEC_Deinit();
        state.vdec_initialized = false;
    }
    if (did_sys) {
        (void)AX_SYS_Deinit();
        state.sys_initialized = false;
    }
    return false;
}

void ShutdownSystem() noexcept {
    std::lock_guard<std::mutex> lock(GetSystemMutex());
    auto& state = GetSystemState();

    if (state.ref_count == 0) {
        return;
    }

    --state.ref_count;
    if (state.ref_count != 0) {
        return;
    }

    if (state.ivps_initialized) {
        (void)AX_IVPS_Deinit();
        state.ivps_initialized = false;
    }
    if (state.venc_initialized) {
        (void)AX_VENC_Deinit();
        state.venc_initialized = false;
    }
    if (state.vdec_initialized) {
        (void)AX_VDEC_Deinit();
        state.vdec_initialized = false;
    }
    if (state.sys_initialized) {
        (void)AX_SYS_Deinit();
        state.sys_initialized = false;
    }
}

bool IsSystemInitialized() noexcept {
    std::lock_guard<std::mutex> lock(GetSystemMutex());
    return GetSystemState().sys_initialized;
}

}  // namespace axvsdk::common
