#include "buan/core/arena_allocator.hpp"
#include "buan/core/types.hpp"

namespace buan {

template <typename T>
auto BuanArena::allocate(size_t count) -> std::expected<T*, ArenaError> {
    const size_t bytes_needed = sizeof(T) * count;
    
    size_t current = m_offset.load(std::memory_order_relaxed);
    size_t aligned;
    
    do {
        aligned = (current + 63) & ~63;
        if (aligned + bytes_needed > m_capacity) {
            return std::unexpected(ArenaError::OUT_OF_MEMORY);
        }
    } while (!m_offset.compare_exchange_weak(current, aligned + bytes_needed,
                                           std::memory_order_release,
                                           std::memory_order_relaxed));

    return reinterpret_cast<T*>(m_base_ptr + aligned);
}

// Fixed explicit instantiation syntax
template std::expected<BuanMarketTick*, ArenaError> BuanArena::allocate<BuanMarketTick>(size_t);

} // namespace buan