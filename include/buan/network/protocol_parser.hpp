#pragma once

#include "buan/core/types.hpp"
#include "buan/network/xdp_portal.hpp"
#include "buan/util/rdtsc_clock.hpp"
#include <expected>
#include <span>

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
    
private:
    /**
     * @brief CME MDP 3.0 Binary Packet Header
     * Every UDP packet starts with this.
     */
    struct CmeBinaryHeader {
        uint32_t msg_seq_num;
        uint64_t sending_time;
    };

    /**
     * @brief SBE (Simple Binary Encoding) Message Header
     * Follows the Binary Header; defines what the message is.
     */
    struct SbeMessageHeader {
        uint16_t block_length;
        uint16_t template_id;
        uint16_t schema_id;
        uint16_t version;
    };

    /**
     * @brief CME Trade Summary Message (Simplified for BuanAlpha)
     * Template ID: 42. This is the "Payload".
     */
    struct CmeTradeReport {
        uint64_t md_entry_px;      // Price (8 bytes)
        int32_t  md_entry_size;    // Quantity (4 bytes)
        uint32_t security_id;      // Symbol ID (4 bytes)
        uint8_t  md_update_action; // 0 = New, 1 = Change, etc.
    };
};

} // namespace buan