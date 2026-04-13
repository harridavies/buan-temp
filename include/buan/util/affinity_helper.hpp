#pragma once

#include <cstdint>
#include <expected>
#include <numa.h>
#include <pthread.h>
#include <sched.h>
#include <fstream>
#include <string>
#include <algorithm>

namespace buan {

enum class AffinityError {
    PINNING_FAILED,
    PRIORITY_ESCALATION_FAILED,
    NUMA_DETECTION_FAILED
};

/**
 * @brief Isolates the calling thread to a specific physical core.
 * * This is the "Jitter Shield." By pinning the ingest thread, we eliminate 
 * the 50,000ns spikes caused by Linux kernel context switches.
 */
[[nodiscard]] inline auto pin_thread(int core_id) noexcept -> std::expected<void, AffinityError> {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        return std::unexpected(AffinityError::PINNING_FAILED);
    }
    return {};
}

/**
 * @brief Elevates thread to SCHED_FIFO (Priority 99).
 * * Ensures the BuanAlpha engine is NEVER preempted by OS background tasks.
 * Requires CAP_SYS_NICE or sudo.
 */
[[nodiscard]] inline auto set_rt_priority() noexcept -> std::expected<void, AffinityError> {
    struct sched_param param{};
    param.sched_priority = 99; // Maximum possible Real-Time priority

    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
        return std::unexpected(AffinityError::PRIORITY_ESCALATION_FAILED);
    }
    return {};
}

/**
 * @brief Returns the NUMA node index of the current execution core.
 * * Critical for ensuring BuanHugePageManager allocates memory on the 
 * same socket as the NIC to avoid QPI/UPI cross-talk latency.
 */
[[nodiscard]] inline auto get_numa_node() noexcept -> int {
    int node = numa_node_of_cpu(sched_getcpu());
    return (node < 0) ? 0 : node;
}

/**
 * @brief Discovers the NUMA node associated with a specific network interface.
 * * Ensures memory allocation is physically adjacent to the NIC hardware.
 */
[[nodiscard]] inline auto get_nic_numa_node(const std::string& ifname) noexcept -> int {
#if defined(__linux__)
    std::string path = "/sys/class/net/" + ifname + "/device/numa_node";
    std::ifstream node_file(path);
    int node = 0;
    if (node_file >> node) {
        // Linux returns -1 if the device isn't NUMA-aware; treat as node 0
        return (node < 0) ? 0 : node;
    }
#endif
    return 0; // Fallback for macOS (M-Series) or failed reads
}

/**
 * @brief Architecture-optimized busy-wait hint.
 * * Prevents "Pipeline Sizzling" during high-frequency polling.
 */
inline void yield_to_hardware() noexcept {
#if defined(__x86_64__)
    asm volatile("pause" ::: "memory");
#elif defined(__aarch64__)
    asm volatile("isb" ::: "memory");
#endif
}

} // namespace buan