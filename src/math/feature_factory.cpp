#include <cstdint>
#include <cmath>
#include "buan/core/market_state_arena.hpp"

#if defined(__x86_64__) && defined(BUAN_AVX512_ENABLED)
#include <immintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace buan {

class FeatureFactory {
public:
    /**
     * @brief Computes Rolling Z-Scores for 8 tickers in parallel.
     * Z = (Price - Mean) / StdDev
     */
    static void compute_z_scores_8x(MarketState* states, const uint16_t* indices) {
#if defined(BUAN_AVX512_ENABLED)
        // 1. Gather Prices from 8 disparate memory locations into one ZMM register
        __m256i v_indices = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(indices));
        
        // Load Means and StdDevs (assuming volatility is pre-stored as 1/StdDev for speed)
        float means[8], inv_stdevs[8], prices[8];
        for(int i=0; i<8; ++i) {
            prices[i] = static_cast<float>(states[indices[i]].last_price.load(std::memory_order_relaxed));
            means[i]  = states[indices[i]].rolling_mean;
            inv_stdevs[i] = 1.0f / (states[indices[i]].volatility + 0.00001f);
        }

        __m256 v_px   = _mm256_loadu_ps(prices);
        __m256 v_mu   = _mm256_loadu_ps(means);
        __m256 v_invs = _mm256_loadu_ps(inv_stdevs);

        // Z = (Price - Mean) * (1/StdDev)
        __m256 v_z = _mm256_mul_ps(_mm256_sub_ps(v_px, v_mu), v_invs);

        // Store back to Arena
        float results[8];
        _mm256_storeu_ps(results, v_z);
        for(int i=0; i<8; ++i) {
            states[indices[i]].rolling_z_score = results[i];
        }

#elif defined(__aarch64__)
        // NEON Path for MacBook M-series
        for (int i = 0; i < 8; i += 4) { // NEON handles 4 floats (128-bit) at a time
            float32x4_t v_px = { 
                static_cast<float>(states[indices[i]].last_price.load()),
                static_cast<float>(states[indices[i+1]].last_price.load()),
                static_cast<float>(states[indices[i+2]].last_price.load()),
                static_cast<float>(states[indices[i+3]].last_price.load())
            };
            float32x4_t v_mu = { 
                states[indices[i]].rolling_mean, states[indices[i+1]].rolling_mean,
                states[indices[i+2]].rolling_mean, states[indices[i+3]].rolling_mean
            };
            float32x4_t v_vol = { 
                states[indices[i]].volatility, states[indices[i+1]].volatility,
                states[indices[i+2]].volatility, states[indices[i+3]].volatility
            };

            // Z = (Price - Mean) / Vol
            float32x4_t v_z = vdivq_f32(vsubq_f32(v_px, v_mu), v_vol);
            
            states[indices[i]].rolling_z_score = vgetq_lane_f32(v_z, 0);
            states[indices[i+1]].rolling_z_score = vgetq_lane_f32(v_z, 1);
            states[indices[i+2]].rolling_z_score = vgetq_lane_f32(v_z, 2);
            states[indices[i+3]].rolling_z_score = vgetq_lane_f32(v_z, 3);
        }
#endif
    }
};

} // namespace buan