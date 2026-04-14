#pragma once

#include <atomic>
#include <unordered_map>
#include <shared_mutex>
#include <optional>
#include <mutex>
#include "buan/core/types.hpp"

namespace buan {

/**
 * @class SymbolIndex
 * @brief Maps disparate External IDs to a dense 0-2047 Internal Index.
 * * Optimized for "Read-Mostly" workloads. Uses a shared_mutex for the 
 * rare event of discovering a new symbol, ensuring the Hot Path 
 * remains contention-free.
 */
class SymbolIndex {
private:
    // We use a 64-bit key to combine (SourceID << 32 | ExternalID)
    std::unordered_map<uint64_t, InternalSymbolID> m_map;
    mutable std::shared_mutex m_mutex;
    std::atomic<uint16_t> m_next_id{0};

    static constexpr uint16_t MAX_SYMBOLS = 2048;

public:
    /**
     * @brief Resolves an external ID to our internal index.
     * @param source_id 0 for CME, 1 for Binance, etc.
     * @param external_id The ID provided by the exchange.
     * @return The internal index, or a new one if not previously seen.
     */
    [[nodiscard]] InternalSymbolID get_or_create(uint32_t source_id, uint32_t external_id) {
        const uint64_t key = (static_cast<uint64_t>(source_id) << 32) | external_id;

        // 1. Fast Path: Optimistic Read
        {
            std::shared_lock lock(m_mutex);
            if (auto it = m_map.find(key); it != m_map.end()) {
                return it->second;
            }
        }

        // 2. Slow Path: Register New Symbol
        std::unique_lock lock(m_mutex);
        
        // Double-check after acquiring write lock
        if (auto it = m_map.find(key); it != m_map.end()) {
            return it->second;
        }

        if (m_next_id >= MAX_SYMBOLS) {
            // In production, we'd log a critical error here.
            return 0; 
        }

        InternalSymbolID new_id = m_next_id.fetch_add(1, std::memory_order_relaxed);
        m_map[key] = new_id;
        return new_id;
    }

    size_t active_count() const noexcept {
        return m_next_id.load(std::memory_order_relaxed);
    }
};

} // namespace buan