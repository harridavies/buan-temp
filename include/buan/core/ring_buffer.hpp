#pragma once

#include <atomic>
#include <cstdint>
#include <new>
#include <type_traits>

namespace buan {

#ifdef __cpp_lib_hardware_interference_size
    using std::hardware_destructive_interference_size;
#else
    constexpr size_t hardware_destructive_interference_size = 64;
#endif

/**
 * @struct BuanDescriptor
 * @brief 16-byte Atomic Unit of Data.
 *
 * Optimized for alignment. 4 descriptors fit exactly into one 64-byte cache line.
 */
struct alignas(16) BuanDescriptor {
    uint64_t addr; 
    uint32_t len;
    uint32_t flags; // Reserved for metadata/circuit-breaker triggers
};

static_assert(sizeof(BuanDescriptor) == 16, "BuanDescriptor must be exactly 16 bytes for cache-line alignment.");
static_assert(std::is_trivially_copyable_v<BuanDescriptor>, "Must be trivially copyable for zero-copy path.");

/**
 * @class BuanRingBuffer
 * @brief Lock-free SPSC Queue with strict Cache-Line Isolation.
 */
template <size_t SIZE = 1024>
class BuanRingBuffer {
    static_assert((SIZE & (SIZE - 1)) == 0, "SIZE must be a power of two for bitwise wrap-around.");

private:
    BuanDescriptor* m_buffer = nullptr;
    bool m_owns_buffer = false;

    // Cache-line 1: Producer Owned (Head)
    alignas(hardware_destructive_interference_size) std::atomic<uint32_t> m_head{0};
    
    // Cache-line 2: Consumer Owned (Tail)
    alignas(hardware_destructive_interference_size) std::atomic<uint32_t> m_tail{0};

public:
    explicit BuanRingBuffer(BuanDescriptor* external_buffer = nullptr)
        : m_buffer(external_buffer), m_owns_buffer(external_buffer == nullptr) {
        if (m_owns_buffer) {
            m_buffer = new BuanDescriptor[SIZE];
        }
    }

    ~BuanRingBuffer() {
        if (m_owns_buffer) delete[] m_buffer;
    }

    [[nodiscard]] __attribute__((always_inline)) auto push(const BuanDescriptor& desc) noexcept -> bool {
        const uint32_t h = m_head.load(std::memory_order_relaxed);
        const uint32_t t = m_tail.load(std::memory_order_acquire);

        if (((h + 1) & (SIZE - 1)) == t) return false;

        m_buffer[h] = desc;
        m_head.store((h + 1) & (SIZE - 1), std::memory_order_release);
        return true;
    }

    [[nodiscard]] __attribute__((always_inline)) auto pop(BuanDescriptor& out_desc) noexcept -> bool {
        const uint32_t t = m_tail.load(std::memory_order_relaxed);
        const uint32_t h = m_head.load(std::memory_order_acquire);

        if (h == t) return false;

        out_desc = m_buffer[t];
        m_tail.store((t + 1) & (SIZE - 1), std::memory_order_release);
        return true;
    }

    // High-performance metrics for the Hela-Audit utility
    [[nodiscard]] auto count() const noexcept -> uint32_t {
        const uint32_t h = m_head.load(std::memory_order_relaxed);
        const uint32_t t = m_tail.load(std::memory_order_relaxed);
        return (h - t) & (SIZE - 1);
    }

    BuanRingBuffer(const BuanRingBuffer&) = delete;
    auto operator=(const BuanRingBuffer&) -> BuanRingBuffer& = delete;
};

} // namespace buan