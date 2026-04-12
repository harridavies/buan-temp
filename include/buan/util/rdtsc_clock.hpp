#pragma once

#include <cstdint>

#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#endif

namespace buan {

/**
 * @class BuanClock
 * @brief Zero-latency hardware timing for the Atomic Ingest path.
 */
class BuanClock {
public:
    /**
     * @brief Raw cycle count. Use for high-frequency telemetry where 
     * reordering overhead must be avoided.
     */
    static auto read() noexcept -> uint64_t {
#if defined(__x86_64__) || defined(__i386__)
        return __rdtsc();
#elif defined(__aarch64__)
        uint64_t val;
        asm volatile("mrs %0, cntvct_el0" : "=r"(val));
        return val;
#else
#error "BuanAlpha: Unsupported architecture."
#endif
    }

    /**
     * @brief Serialized cycle count. Use for the Hela-Audit benchmark 
     * to ensure the timestamp is captured exactly at the instruction boundary.
     */
    static auto read_precise() noexcept -> uint64_t {
#if defined(__x86_64__) || defined(__i386__)
        unsigned int aux;
        return __rdtscp(&aux);
#elif defined(__aarch64__)
        uint64_t val;
        asm volatile("isb; mrs %0, cntvct_el0" : "=r"(val));
        return val;
#endif
    }

    /**
     * @brief Full hardware memory fence.
     * * Ensures all previous memory operations are globally visible 
     * before the next Atomic Signal update.
     */
    static void fence() noexcept {
#if defined(__x86_64__) || defined(__i386__)
        asm volatile("mfence" ::: "memory");
#elif defined(__aarch64__)
        asm volatile("dmb ish" ::: "memory");
#endif
    }
};

} // namespace buan