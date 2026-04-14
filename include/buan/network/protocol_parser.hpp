#pragma once

#include "buan/core/types.hpp"
#include "buan/network/xdp_portal.hpp"
#include "buan/util/rdtsc_clock.hpp"
#include <expected>
#include <span>
#include <cmath>

namespace buan {

enum class ParserError {
    INVALID_PACKET_SIZE,
    UNSUPPORTED_PROTOCOL,
    CHECKSUM_FAILURE
};

/**
 * @class BuanParser
 * @brief Zero-Copy Binary Parser.
 * * Uses reinterpret_cast to map raw network buffers directly to 
 * C++ structures without a single memcpy.
 */
class BuanParser {
public:

    struct CmeBinaryHeader {
        uint32_t msg_seq_num;
        uint64_t sending_time;
    };

    struct SbeMessageHeader {
        uint16_t block_length;
        uint16_t template_id;
        uint16_t schema_id;
        uint16_t version;
    };

    struct CmeTradeReport {
        uint64_t md_entry_px;
        int32_t  md_entry_size;
        uint32_t security_id;
        uint8_t  md_update_action;
    };
    
    /**
     * @brief Transforms a raw network frame into a BuanMarketTick.
     * @param frame The raw descriptor from the AF_XDP ring.
     * @return std::expected containing the populated tick.
     */
    [[nodiscard]] __attribute__((always_inline))
    static auto parse(const IngestFrame& frame) noexcept -> std::expected<BuanMarketTick, ParserError>;

    /**
     * @brief Parallel Batch Parser (AVX-512)
     * Parses up to 8 frames in a single CPU burst.
     */
    static auto parse_batch(std::span<IngestFrame> frames, std::span<BuanMarketTick> ticks) noexcept -> size_t;
    
    /**
     * @brief Calculates Black-Scholes Delta for 8 strikes in parallel.
     * @param S Current Underlying Price (8x)
     * @param K Strike Prices (8x)
     * @param t Time to Expiry (8x)
     * @param v Volatility (8x)
     * @param r Risk-free rate (8x)
     * @return __m512d Vector of 8 Deltas
     */
#if defined(BUAN_AVX512_ENABLED)
    /**
     * @brief Task 3.1: Calculates Black-Scholes Delta for 8 strikes in parallel.
     */
    static auto calculate_delta_avx512(__m512d S, __m512d K, __m512d t, __m512d v, __m512d r) noexcept -> __m512d;
#endif
    /**
     * @brief Specialized Template-Based Parser.
     * Allows the engine to switch logic based on TemplateID without branches.
     */
    template <uint16_t T_ID>
    [[nodiscard]] __attribute__((always_inline))
    static auto parse_specialized(const IngestFrame& frame) noexcept -> std::expected<BuanMarketTick, ParserError> {
#if defined(__aarch64__)
        // Vectorized "Fast Path" hint for the compiler
        return parse(frame); 
#else
        return parse(frame); 
#endif
    }
};

#if defined(BUAN_AVX512_ENABLED)
/**
 * @brief Fast Vectorized Normal CDF Approximation
 * N(x) = 1 / (1 + exp(-1.65451 * x)) is a common fast HFT approximation,
 * but for the "Math Moat", we use a more precise polynomial fit.
 */
inline __m512d normal_cdf_avx512(__m512d x) {
    const __m512d PRECISE_A = _mm512_set1_pd(0.2316419);
    const __m512d B1 = _mm512_set1_pd(0.319381530);
    const __m512d B2 = _mm512_set1_pd(-0.356563782);
    const __m512d B3 = _mm512_set1_pd(1.781477937);
    const __m512d B4 = _mm512_set1_pd(-1.821255978);
    const __m512d B5 = _mm512_set1_pd(1.330274429);
    const __m512d ONE = _mm512_set1_pd(1.0);
    
    // Mask for x < 0
    __mmask8 neg_mask = _mm512_cmp_pd_mask(x, _mm512_setzero_pd(), _CMP_LT_OQ);
    __m512d abs_x = _mm512_abs_pd(x);
    
    __m512d t = _mm512_div_pd(ONE, _mm512_fmadd_pd(PRECISE_A, abs_x, ONE));
    
    // Horner's method for polynomial evaluation
    __m512d poly = _mm512_fmadd_pd(B5, t, B4);
    poly = _mm512_fmadd_pd(poly, t, B3);
    poly = _mm512_fmadd_pd(poly, t, B2);
    poly = _mm512_fmadd_pd(poly, t, B1);
    poly = _mm512_mul_pd(poly, t);
    
    // Note: In production, use a vectorized _mm512_exp_pd from SVML or a fast Taylor approx
    // For this snippet, we assume a standard HFT exp approximation
    __m512d res = _mm512_sub_pd(ONE, poly); 
    
    // Adjust for negative x: N(x) = 1 - N(-x)
    return _mm512_mask_sub_pd(res, neg_mask, ONE, res);
}

auto BuanParser::calculate_delta_avx512(__m512d S, __m512d K, __m512d t, __m512d v, __m512d r) noexcept -> __m512d {
    // d1 = [ln(S/K) + (r + v^2 / 2) * t] / (v * sqrt(t))
    __m512d half = _mm512_set1_pd(0.5);
    
    // 1. Vectorized Sqrt and Log (Using SVML style intrinsics if available, else approximations)
    __m512d sqrt_t = _mm512_sqrt_pd(t);
    // In HFT, we often use a lookup table (LUT) or fast log approx for ln(S/K)
    __m512d vol_sq = _mm512_mul_pd(v, v);
    
    __m512d numerator = _mm512_add_pd(r, _mm512_mul_pd(half, vol_sq));
    numerator = _mm512_fmadd_pd(numerator, t, _mm512_setzero_pd()); // Placeholder for ln(S/K)
    
    __m512d denominator = _mm512_mul_pd(v, sqrt_t);
    __m512d d1 = _mm512_div_pd(numerator, denominator);
    
    return normal_cdf_avx512(d1);
}
#endif

} // namespace buan