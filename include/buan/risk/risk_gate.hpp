#pragma once

#include <atomic>
#include <cstdint>

namespace buan {

/**
 * @class BuanRiskGate
 * @brief The "Final Guard" - Ultra-low latency pre-trade risk engine.
 * Performs branchless validation of price and position limits.
 */
class BuanRiskGate {
private:
    std::atomic<int64_t> m_net_position{0};
    int64_t m_max_pos;
    int64_t m_min_price;
    int64_t m_max_price;

public:
    /**
     * @brief Initialize risk limits.
     * @param max_pos Absolute limit for net position (e.g., 10000).
     * @param min_px Minimum allowed price in fixed-point.
     * @param max_px Maximum allowed price in fixed-point.
     */
    explicit BuanRiskGate(int64_t max_pos, int64_t min_px, int64_t max_px)
        : m_max_pos(max_pos), m_min_price(min_px), m_max_price(max_px) {}

    /**
     * @brief Branchless verification of price and position.
     * Uses unsigned subtraction range checks to minimize CPU branching.
     * Task 8.1 & 8.2
     */
    [[nodiscard]] __attribute__((always_inline))
    bool verify(int64_t price, uint32_t volume, uint8_t side) noexcept {
        // 1. Fat-Finger Price Check (Branchless corridor check)
        // Checks if price is in [m_min_price, m_max_price]
        const bool price_ok = static_cast<uint64_t>(price - m_min_price) <= 
                              static_cast<uint64_t>(m_max_price - m_min_price);

        // 2. Stateful Position Check (Task 8.2)
        // side: 1 = Buy, 2 = Sell. side_sign: 1 for Buy, -1 for Sell.
        const int64_t side_sign = 3 - (static_cast<int64_t>(side) << 1); 
        const int64_t current_pos = m_net_position.load(std::memory_order_relaxed);
        const int64_t new_pos = current_pos + (side_sign * static_cast<int64_t>(volume));
        
        // Checks if new_pos is in [-m_max_pos, m_max_pos]
        const bool pos_ok = static_cast<uint64_t>(new_pos + m_max_pos) <= 
                            static_cast<uint64_t>(m_max_pos << 1);

        return price_ok & pos_ok;
    }

    /**
     * @brief Atomic update of the net position.
     */
    void update_position(uint32_t volume, uint8_t side) noexcept {
        const int64_t side_sign = 3 - (static_cast<int64_t>(side) << 1);
        m_net_position.fetch_add(side_sign * static_cast<int64_t>(volume), std::memory_order_relaxed);
    }

    [[nodiscard]] int64_t get_position() const noexcept {
        return m_net_position.load(std::memory_order_relaxed);
    }
};

} // namespace buan