#include "buan/network/protocol_parser.hpp"

namespace buan {

auto BuanParser::parse(const IngestFrame& frame) noexcept -> std::expected<BuanMarketTick, ParserError> {
    // 1. Establish the high-precision ingress timestamp IMMEDIATELY
    const uint64_t ts = BuanClock::read_precise();

    // 2. Map the raw address to our expected network header
    // No memcpy. Just a pointer adjustment.
    const auto* raw = reinterpret_cast<const RawFeedHeader*>(frame.addr);

    // 3. Bounds check (The only branch in the hot path)
    if (frame.len < sizeof(RawFeedHeader)) {
        return std::unexpected(ParserError::INVALID_PACKET_SIZE);
    }

    // 4. Populate the tick structure
    // We are converting the wire-format (raw) to our internal 64-byte tick.
    BuanMarketTick tick;
    tick.ingress_tsc = ts;
    tick.symbol_id   = raw->stream_id;
    tick.price       = raw->price_raw;
    tick.volume      = raw->volume_raw;
    tick.signal_drift = 0.0f; // To be populated by the AI logic later
    tick.flags       = 0;

    return tick;
}

} // namespace buan