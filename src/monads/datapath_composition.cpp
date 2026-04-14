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
                    
                    // Task 1.2: Map External ID to Internal Index
                    // Source 0 = CME/Parser, Source 1 = Binance (from flags)
                    uint32_t source = (tick.flags == 1) ? 1 : 0;
                    tick.internal_id = m_symbol_index.get_or_create(source, tick.symbol_id);

                    // Task 2.2: Update the Global Market Arena (Shared Knowledge Base)
                    auto& slot = m_market_arena.get_slot(tick.internal_id);
                    
                    // 1. Update Basic State
                    (void)slot.last_price.exchange(tick.price, std::memory_order_relaxed);
                    slot.last_volume.store(tick.volume, std::memory_order_relaxed);

                    // 2. Task 3.1: Incremental Feature Update (Welford-style for speed)
                    // This happens in the Hot Path for EVERY tick.
                    float price_f = static_cast<float>(tick.price);
                    float delta = price_f - slot.rolling_mean;
                    
                    // Simple EMA (alpha = 0.01 for smoothing)
                    slot.rolling_mean += 0.01f * delta;
                    float delta2 = price_f - slot.rolling_mean;
                    slot.rolling_m2 += 0.01f * (delta * delta2);
                    
                    // Calculate Volatility (StdDev) and Z-Score
                    slot.volatility = std::sqrt(slot.rolling_m2);
                    if (slot.volatility > 0.0001f) {
                        slot.rolling_z_score = (price_f - slot.rolling_mean) / slot.volatility;
                    }

                    // Task 7.2: Capture "Factor Completion" Timestamp
                    const uint64_t factor_tsc = BuanClock::read_precise();
                    
                    slot.last_volume.store(tick.volume, std::memory_order_relaxed);
                    slot.last_update_tsc_low.store(static_cast<uint32_t>(tick.ingress_tsc), std::memory_order_relaxed);
                    
                    // Update Risk Engine with latest price for banding calculations
                    m_risk_gate.update_market_price(tick.price);

                    // 1. Multi-Threshold Circuit Breaker Logic
                    bool is_safe = (tick.signal_drift <= m_drift_threshold) &&
                                   (tick.volume <= m_max_vol_limit);

                    if (!is_safe) {
                        return std::unexpected(EngineStatus::FILTERED_BY_BREAKER);
                    }

                    // 2. The Sovereign Guard: Detailed Risk Check
                    // We check if a hypothetical 'Buy' order (Side 1) is safe for this signal
                    if (!m_risk_gate.check(tick.price, tick.volume, 1)) {
                        return std::unexpected(EngineStatus::FILTERED_BY_RISK);
                    }
                    
                    // 3. Auditing Path (Shadow Log)
                    (void)m_audit_ring.push(BuanAuditDescriptor{
                        .ingress_tsc = tick.ingress_tsc,
                        .factor_tsc = factor_tsc,
                        .symbol_id = tick.symbol_id,
                        .flags = tick.flags,
                        .padding = {0} // Add this line
                    });

                    // 4. Dispatch to Hot Path
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