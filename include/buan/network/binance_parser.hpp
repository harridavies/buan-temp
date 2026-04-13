#pragma once

#include "buan/core/types.hpp"
#include "buan/network/xdp_portal.hpp"
#include <expected>

namespace buan {

/**
 * @class BinanceParser
 * @brief High-speed binary parser for Binance Futures.
 * * Designed for the "Viking Mode" binary stream to bypass JSON overhead.
 */
class BinanceParser {
public:
    enum class BinanceError {
        INVALID_HEADER,
        INCOMPLETE_PAYLOAD,
        UNSUPPORTED_TEMPLATE
    };

    /**
     * @brief Parses a Binance Binary Trade event.
     */
    [[nodiscard]] __attribute__((always_inline))
    static auto parse_trade(const IngestFrame& frame) noexcept -> std::expected<BuanMarketTick, BinanceError>;

private:
    struct BinanceBinaryHeader {
        uint16_t msg_length;
        uint16_t template_id; // e.g., 100 for Trade
    };

    struct BinanceTradeReport {
        uint64_t trade_id;
        uint64_t price;
        uint64_t quantity;
        uint32_t symbol_hash;
    };
};

} // namespace buan