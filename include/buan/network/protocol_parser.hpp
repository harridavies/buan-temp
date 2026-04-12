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

private:
    // Mock header for a 2026 Alternative Data Feed (e.g., UDP Binary)
    struct RawFeedHeader {
        uint32_t magic;
        uint32_t stream_id;
        uint32_t sequence;
        int64_t  price_raw;
        uint32_t volume_raw;
    };
};

} // namespace buan