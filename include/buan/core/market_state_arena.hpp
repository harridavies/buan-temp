#pragma once

#include <atomic>
#include <cstdint>
#include <vector>
#include <string>
#include "buan/core/types.hpp"
#include "buan/core/hugepage_manager.hpp"

namespace buan {

/**
 * @struct MarketState
 * @brief The "Intelligence Slot" for a single ticker.
 * * Aligned to 64 bytes to prevent False Sharing.
 */
struct alignas(64) MarketState {
    // Basic Price Info (128-bit block)
    std::atomic<int64_t> last_price{0};
    std::atomic<uint32_t> last_volume{0};
    std::atomic<uint32_t> last_update_tsc_low{0}; // Split for atomic ease
    
    // Feature State (Calculated by Feature Factory)
    float rolling_mean{0.0f};
    float rolling_m2{0.0f};       // Sum of squares for Variance
    float rolling_z_score{0.0f};
    float volatility{0.0f};
    
    // Cross-Asset State (e.g., Correlation to a Benchmark like BTC or SPY)
    float correlation_benchmark{0.0f}; 
    float reserved_features[7];   // Total size check: 8+4+4 + 4*4 + 4 + 7*4 = 64 bytes
};

/**
 * @class MarketStateArena
 * @brief HugePage-backed global state map for 2,048 tickers.
 */
class MarketStateArena {
private:
    static constexpr size_t MAX_TICKERS = 2048;
    BuanHugePageManager m_hp_manager;
    MarketState* m_states{nullptr};

public:
    explicit MarketStateArena(int numa_node = 0, const std::string& name = "/buan_market_arena") 
        : m_hp_manager(MAX_TICKERS * sizeof(MarketState), numa_node) {
        
        auto alloc_res = m_hp_manager.allocate(name);
        if (alloc_res) {
            m_states = static_cast<MarketState*>(*alloc_res);
            // Zero-out the memory to initialize atomics
            std::memset(m_states, 0, MAX_TICKERS * sizeof(MarketState));
        }
    }

    /**
     * @brief Direct O(1) access to a ticker's intelligence slot.
     */
    [[nodiscard]] __attribute__((always_inline))
    MarketState& get_slot(InternalSymbolID id) noexcept {
        return m_states[id];
    }

    /**
     * @brief Const access for read-only risk checks. 
     * Ensures we don't accidentally modify state during the check phase.
     */
    [[nodiscard]] __attribute__((always_inline))
    const MarketState& get_slot_const(InternalSymbolID id) const noexcept {
        return m_states[id];
    }

    /**
     * @brief Returns raw pointer for Python Zero-Copy exposure (Task 5).
     */
    [[nodiscard]] MarketState* raw_ptr() noexcept { return m_states; }
    
    [[nodiscard]] size_t size_bytes() const noexcept { return MAX_TICKERS * sizeof(MarketState); }
};

} // namespace buan