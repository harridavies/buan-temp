#pragma once

#include <vector>
#include <algorithm>
#include <cstdint>

namespace buan {

struct VolPoint {
    int64_t strike;
    float   iv;
};

/**
 * @class VolSurface
 * @brief Linear interpolator for Volatility Surface.
 * * Optimized for cache-locality by keeping points in a contiguous vector.
 */
class VolSurface {
private:
    std::vector<VolPoint> m_points; // Sorted by strike

public:
    /**
     * @brief Updates or inserts a volatility point.
     * Keep the surface sorted to allow O(log N) lookup.
     */
    void update(int64_t strike, float iv) {
        auto it = std::lower_bound(m_points.begin(), m_points.end(), strike, 
            [](const VolPoint& p, int64_t s) { return p.strike < s; });
            
        if (it != m_points.end() && it->strike == strike) {
            it->iv = iv;
        } else {
            m_points.insert(it, {strike, iv});
        }
    }

    /**
     * @brief Linear interpolation of IV for a given strike.
     * Hot Path: Returns the exact IV or an interpolated value.
     */
    [[nodiscard]] __attribute__((always_inline))
    float get_iv(int64_t strike) const noexcept {
        if (m_points.empty()) return 0.0f;
        if (strike <= m_points.front().strike) return m_points.front().iv;
        if (strike >= m_points.back().strike) return m_points.back().iv;

        auto it = std::lower_bound(m_points.begin(), m_points.end(), strike,
            [](const VolPoint& p, int64_t s) { return p.strike < s; });

        // Linear interpolation: y = y0 + (x - x0) * (y1 - y0) / (x1 - x0)
        const auto& p1 = *it;
        const auto& p0 = *(it - 1);
        
        float slope = (p1.iv - p0.iv) / static_cast<float>(p1.strike - p0.strike);
        return p0.iv + static_cast<float>(strike - p0.strike) * slope;
    }
};

} // namespace buan