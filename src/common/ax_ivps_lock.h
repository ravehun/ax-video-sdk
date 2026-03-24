#pragma once

#include <mutex>

namespace axvsdk::common::internal {

// AX_IVPS VPP/Draw APIs are not consistently thread-safe across MSP/driver versions.
// Serialize IVPS operations in-process to avoid rare crashes under multi-threaded workloads
// (e.g. NPU pre-process + OSD draw concurrently).
inline std::mutex& IvpsGlobalMutex() noexcept {
    static std::mutex mu;
    return mu;
}

}  // namespace axvsdk::common::internal

