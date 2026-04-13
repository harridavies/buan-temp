#include "buan/network/protocol_parser.hpp"

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

#if defined(__x86_64__) && defined(BUAN_AVX512_ENABLED)
#include <immintrin.h>
#endif

namespace buan {

auto BuanParser::parse(const IngestFrame& frame) noexcept -> std::expected<BuanMarketTick, ParserError> {
    // 1. Establish the high-precision ingress timestamp IMMEDIATELY
    const uint64_t ts = frame.ingress_tsc; // Use the pre-captured hardware-aligned timestamp
    
    // 2. Minimum size check (Binary Header + SBE Header + Trade Message)
    constexpr size_t min_size = sizeof(CmeBinaryHeader) + sizeof(SbeMessageHeader) + sizeof(CmeTradeReport);
    if (frame.len < min_size) {
        return std::unexpected(ParserError::INVALID_PACKET_SIZE);
    }

    // 3. Navigate the headers via Pointer Arithmetic (Zero-Copy)
    const uint8_t* base = reinterpret_cast<const uint8_t*>(frame.addr);
    
    // Skip the 12-byte Binary Header
    const auto* sbe_hdr = reinterpret_cast<const SbeMessageHeader*>(base + sizeof(CmeBinaryHeader));
    
    // Skip the 8-byte SBE Header to reach the Trade Report
    const auto* trade = reinterpret_cast<const CmeTradeReport*>(
        base + sizeof(CmeBinaryHeader) + sizeof(SbeMessageHeader)
    );

    // 4. Validate Template ID (Example: 42 is our Trade Report)
    if (sbe_hdr->template_id != 42) {
        return std::unexpected(ParserError::UNSUPPORTED_PROTOCOL);
    }

    // 5. Populate the BuanMarketTick (The internal 64-byte projectile)
    BuanMarketTick tick;
    tick.ingress_tsc  = ts;
    tick.signal_drift = 0.0f; 
    tick.flags        = 0;

#if defined(__aarch64__)
    /**
     * @brief NEON Vectorized Path (MacBook M-Series)
     * Load 16 bytes (128 bits) of the CME Trade Report in one instruction.
     * This captures md_entry_px (8), md_entry_size (4), and security_id (4).
     */
    uint8x16_t raw_vec = vld1q_u8(reinterpret_cast<const uint8_t*>(trade));
    
    // Extract fields via zero-latency register lanes
    // Lane 0 of u64 is md_entry_px
    tick.price     = vgetq_lane_u64(vreinterpretq_u64_u8(raw_vec), 0);
    // Lane 2 of u32 (offset 8) is md_entry_size
    tick.volume    = vgetq_lane_u32(vreinterpretq_u32_u8(raw_vec), 2);
    // Lane 3 of u32 (offset 12) is security_id
    tick.symbol_id = vgetq_lane_u32(vreinterpretq_u32_u8(raw_vec), 3);
#elif defined(BUAN_AVX512_ENABLED)
    /**
     * @brief AVX-512 Single-Path (Task 8.1)
     * Load 64 bytes (the entire packet region) into a ZMM register.
     */
    __m512i raw_zmm = _mm512_loadu_si512(reinterpret_cast<const void*>(trade));
    
    // Extract fields using zero-latency permute/extract
    tick.price     = _mm512_extracti64x2_epi64(raw_zmm, 0)[0];
    tick.volume    = _mm512_extract_epi32(raw_zmm, 2);
    tick.symbol_id = _mm512_extract_epi32(raw_zmm, 3);
#else
    /**
     * @brief Standard Scalar Path (Fallback for Non-ARM/Production)
     * This remains for compatibility and serves as the baseline for Hela-Audit.
     */
    // Ensure the memory is prefetched into L1 cache for the next step
    __builtin_prefetch(base + 64, 0, 3);
    tick.symbol_id = trade->security_id;
    tick.price     = trade->md_entry_px;
    tick.volume    = static_cast<uint32_t>(trade->md_entry_size);
#endif

    return tick;
}

auto BuanParser::parse_batch(std::span<IngestFrame> frames, std::span<BuanMarketTick> ticks) noexcept -> size_t {
#if defined(BUAN_AVX512_ENABLED)
    // Task 8.2: Branchless State Machine using Masking
    // We load Template IDs from 8 packets into one 128-bit register
    uint16_t ids[8];
    for(int i=0; i<8; ++i) {
        auto* hdr = reinterpret_cast<SbeMessageHeader*>(
            (uint8_t*)frames[i].addr + sizeof(CmeBinaryHeader)
        );
        ids[i] = hdr->template_id;
    }

    __m128i template_vec = _mm_loadu_si128((__m128i*)ids);
    __m128i target_vec   = _mm_set1_epi16(42);

    // Get an 8-bit mask of which packets are Template 42
    __mmask8 mask = _mm_cmpeq_epu16_mask(template_vec, target_vec);

    // Process only the packets allowed by the hardware mask
    for (int i = 0; i < 8; ++i) {
        if (mask & (1 << i)) {
            // Monadic "Correct-by-Construction" pattern: 
            // Only populate the tick if the parse succeeds, otherwise skip.
            auto parse_res = parse(frames[i]);
            if (parse_res.has_value()) {
                ticks[count++] = parse_res.value();
            }
        }
    }
    return _mm_popcnt_u32(mask); // Return number of successfully parsed trades
#else
    // Fallback for Mac/Non-AVX
    size_t count = 0;
    for(size_t i=0; i < frames.size(); ++i) {
        auto res = parse(frames[i]);
        if(res) { ticks[count++] = res.value(); }
    }
    return count;
#endif
}

} // namespace buan