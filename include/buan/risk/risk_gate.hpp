#pragma once

#include <atomic>
#include <cstdint>
#include "buan/util/rdtsc_clock.hpp"
#include "buan/core/market_state_arena.hpp"

namespace buan {

/**
 * @class BuanRiskEngine
 * @brief The Sovereign Guard: Comprehensive pre-trade risk management.
 * Performs multi-stage, branchless validation in <10ns.
 */
class BuanRiskEngine {
private:
    // Position State
    std::atomic<int64_t> m_net_position{0};
    const int64_t m_max_pos;
    
    // Price State
    std::atomic<int64_t> m_last_market_price{0};
    const int64_t m_max_price_drift_bps; // Max drift in Basis Points (e.g., 500 = 5%)
    
    // Volume State
    const uint32_t m_max_order_size;

    // Global Awareness
    const MarketStateArena* m_arena{nullptr};
    InternalSymbolID m_benchmark_id{0}; // e.g., Slot for BTC-USDT
    const float m_panic_z_threshold{-3.0f}; // Trigger "Tighten" at -3 Sigma

    // Rate Limiting (Token Bucket approach)
    std::atomic<uint64_t> m_last_token_tsc{0};
    std::atomic<int32_t>  m_tokens{0};
    const int32_t         m_max_tokens;
    const uint64_t        m_refill_period_cycles;

public:
    explicit BuanRiskEngine(int64_t max_pos, 
                            int64_t max_drift_bps, 
                            uint32_t max_qty,
                            int32_t max_rate_count,
                            const MarketStateArena* arena = nullptr,
                            InternalSymbolID benchmark = 0)
        : m_net_position(0), // Good practice to init atomics here if not in header
          m_max_pos(max_pos), 
          m_max_price_drift_bps(max_drift_bps),
          m_max_order_size(max_qty),
          m_arena(arena),
          m_benchmark_id(benchmark),
          m_max_tokens(max_rate_count),
          m_refill_period_cycles(3000000) {
              m_tokens.store(max_rate_count);
          }

    /**
     * @brief Branchless multi-check. 
     * returns 1 if safe, 0 if risk breach.
     */
    [[nodiscard]] __attribute__((always_inline))
    bool check(int64_t price, uint32_t volume, uint8_t side) noexcept {
        // 0. Correlated Risk Check (The Sovereign Advantage)
        uint32_t dynamic_max_qty = m_max_order_size;
        
        if (m_arena) {
            const auto& benchmark_slot = m_arena->get_slot_const(m_benchmark_id);
            // If the benchmark (BTC) is in a tail-spin, tighten limits
            if (benchmark_slot.rolling_z_score < m_panic_z_threshold) {
                dynamic_max_qty /= 2; // Instant 50% reduction in risk appetite
            }
        }

        // 1. Max Order Size Check (Now uses dynamic_max_qty)
        const bool size_ok = volume <= dynamic_max_qty;

        // 2. Stateful Position Check
        const int64_t side_sign = 3 - (static_cast<int64_t>(side) << 1); 
        const int64_t current_pos = m_net_position.load(std::memory_order_relaxed);
        const int64_t new_pos = current_pos + (side_sign * static_cast<int64_t>(volume));
        const bool pos_ok = static_cast<uint64_t>(new_pos + m_max_pos) <= 
                            static_cast<uint64_t>(m_max_pos << 1);

        // 3. Price Banding Check
        const int64_t last_px = m_last_market_price.load(std::memory_order_relaxed);
        const bool price_ok = (last_px == 0) || 
            (static_cast<uint64_t>(std::abs(price - last_px)) * 10000 <= 
             static_cast<uint64_t>(last_px * m_max_price_drift_bps));

        // 4. Rate Limit Check
        const bool rate_ok = m_tokens.load(std::memory_order_relaxed) > 0;
        
        return size_ok & pos_ok & price_ok & rate_ok;
    }

    /**
     * @brief Updates engine state after a successful "Strike".
     */
    void on_order_sent(int64_t /*price*/, uint32_t volume, uint8_t side) noexcept {
        const int64_t side_sign = 3 - (static_cast<int64_t>(side) << 1);
        m_net_position.fetch_add(side_sign * volume, std::memory_order_relaxed);
        m_tokens.fetch_sub(1, std::memory_order_relaxed);
    }

    /**
     * @brief Updates market price reference from incoming market data.
     */
    void update_market_price(int64_t price) noexcept {
        m_last_market_price.store(price, std::memory_order_relaxed);
        
        // Background token refill logic
        uint64_t now = BuanClock::read();
        if (now - m_last_token_tsc.load(std::memory_order_relaxed) > m_refill_period_cycles) {
            m_tokens.store(m_max_tokens, std::memory_order_relaxed);
            m_last_token_tsc.store(now, std::memory_order_relaxed);
        }
    }
};

} // namespace buan