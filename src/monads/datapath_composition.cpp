#include "buan/monads/engine.hpp"
#include "buan/network/binance_parser.hpp"
#include "buan/risk/risk_gate.hpp"
#include <cmath>

namespace buan {

auto BuanEngine::step() noexcept -> EngineStatus {
    return m_portal.poll_frame()
        .transform_error([](auto) { return EngineStatus::PORTAL_EMPTY; })
        .and_then([&](IngestFrame frame) -> std::expected<EngineStatus, EngineStatus> {
            
            // Branchless dispatch between CME and Binance based on frame size
            auto parse_result = (frame.len > 100) 
                ? BuanParser::parse(frame) 
                : (std::expected<BuanMarketTick, ParserError>)BinanceParser::parse_trade(frame)
                    .transform_error([](auto){ return ParserError::UNSUPPORTED_PROTOCOL; });

            return parse_result
                .transform_error([](auto) { return EngineStatus::IDLE; })
                .and_then([&](BuanMarketTick tick) -> std::expected<EngineStatus, EngineStatus> {
                    
                    // 1. Multi-Threshold Circuit Breaker Logic
                    bool is_safe = (tick.signal_drift <= m_drift_threshold) &&
                                   (tick.volume <= m_max_vol_limit) &&
                                   (std::abs(tick.price) < (1LL << 62));

                    if (!is_safe) {
                        return std::unexpected(EngineStatus::FILTERED_BY_BREAKER);
                    }

                    // Task 8.3: Monadic Risk Integration
                    // Check if the proposed tick (or resulting trade) violates risk limits.
                    // We assume Side=1 (Buy) for the logic check in this polling cycle.
                    if (!m_risk_gate.verify(tick.price, tick.volume, 1)) {
                        return std::unexpected(EngineStatus::FILTERED_BY_RISK);
                    }
                    
                    // 2. CAR 2026: Push to the Shadow Log (Auditing Path)
                    // Cast to void to suppress [[nodiscard]] warning on the audit path
                    (void)m_audit_ring.push(BuanAuditDescriptor{
                        .ingress_tsc = tick.ingress_tsc,
                        .symbol_id = tick.symbol_id,
                        .flags = tick.flags
                    });

                    // 3. Prepare Descriptor for the Hot Path
                    BuanDescriptor desc{ 
                        .addr = reinterpret_cast<uint64_t>(frame.addr), 
                        .len = sizeof(BuanMarketTick), 
                        .flags = tick.symbol_id 
                    };
                    
                    if (m_ring.push(desc)) {
                        return EngineStatus::SIGNAL_CAPTURED;
                    }
                    return std::unexpected(EngineStatus::BUFFER_FULL);
                });
        }).value_or(EngineStatus::PORTAL_EMPTY);
}

} // namespace buan