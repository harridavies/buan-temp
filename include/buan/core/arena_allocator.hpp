#pragma once

#include <atomic>
#include <cstdint>
#include <expected>

namespace buan {

enum class ArenaError {
    OUT_OF_MEMORY,
    ALIGNMENT_FAILURE
};

/**
 * @class BuanArena
 * @brief Zero-Lock Linear Memory Arena.
 * * Manages memory within the HugePage region. Allocation is a simple atomic 
 * pointer bump, making it significantly faster than any standard allocator.
 */
class BuanArena {
private:
    uint8_t* m_base_ptr;
    size_t   m_capacity;
    std::atomic<size_t> m_offset{0};

public:
    BuanArena(void* base, size_t size) 
        : m_base_ptr(static_cast<uint8_t*>(base)), m_capacity(size) {}

    /**
     * @brief Atomically claims a block of memory.
     */
    template <typename T>
    auto allocate(size_t count = 1) -> std::expected<T*, ArenaError>;

    void reset() noexcept { m_offset.store(0, std::memory_order_release); }

    [[nodiscard]] auto used_bytes() const noexcept -> size_t { 
        return m_offset.load(std::memory_order_relaxed); 
    }
};

} // namespace buan