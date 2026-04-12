#include "buan/monads/engine.hpp"

namespace buan {

auto BuanEngine::step() noexcept -> EngineStatus {
    
    auto result = m_portal.poll_frame()
        .transform_error([](auto) { return EngineStatus::PORTAL_EMPTY; })
        
        .and_then([&](IngestFrame frame) -> std::expected<BuanMarketTick, EngineStatus> {
            // Explicitly map ParserError to EngineStatus::IDLE
            auto tick_res = BuanParser::parse(frame);
            if (!tick_res) {
                m_portal.release_frame(frame.addr);
                return std::unexpected(EngineStatus::IDLE);
            }
            return tick_res.value();
        })
        
        .and_then([&](BuanMarketTick tick) -> std::expected<BuanMarketTick, EngineStatus> {
            if (tick.signal_drift > m_drift_threshold) {
                return std::unexpected(EngineStatus::FILTERED_BY_BREAKER);
            }
            return tick;
        })
        
        .and_then([&](BuanMarketTick tick) -> std::expected<EngineStatus, EngineStatus> {
            BuanDescriptor desc{ 
                .addr = tick.ingress_tsc, 
                .len = tick.symbol_id, 
                .flags = tick.volume 
            };
            
            if (m_ring.push(desc)) {
                return EngineStatus::SIGNAL_CAPTURED;
            }
            return std::unexpected(EngineStatus::BUFFER_FULL);
        });

    return result.has_value() ? result.value() : result.error();
}

} // namespace buan