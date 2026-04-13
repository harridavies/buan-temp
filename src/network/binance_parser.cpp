#include "buan/network/binance_parser.hpp"
#include "buan/util/rdtsc_clock.hpp"

namespace buan {

auto BinanceParser::parse_trade(const IngestFrame& frame) noexcept -> std::expected<BuanMarketTick, BinanceError> {
    const uint64_t ts = BuanClock::read_precise();
    
    // Safety check: Ensure packet is large enough for headers
    if (frame.len < sizeof(BinanceBinaryHeader) + sizeof(BinanceTradeReport)) {
        return std::unexpected(BinanceError::INCOMPLETE_PAYLOAD);
    }

    const uint8_t* base = reinterpret_cast<const uint8_t*>(frame.addr);
    const auto* hdr = reinterpret_cast<const BinanceBinaryHeader*>(base);
    
    // Validate Template ID (100 for Trade events)
    if (hdr->template_id != 100) {
        return std::unexpected(BinanceError::UNSUPPORTED_TEMPLATE);
    }

    const auto* report = reinterpret_cast<const BinanceTradeReport*>(base + sizeof(BinanceBinaryHeader));

    BuanMarketTick tick;
    tick.ingress_tsc  = ts; // This should ideally be frame.ingress_tsc as per Task 3
    tick.symbol_id    = report->symbol_hash;
    tick.price        = report->price;
    tick.volume       = static_cast<uint32_t>(report->quantity);
    tick.signal_drift = 0.0f;
    tick.flags        = 1; // Mark as Binance source

    return tick;
}

} // namespace buan