#pragma once

#include "buan/network/portal_interface.hpp"
#include "buan/network/xdp_portal.hpp"
#include "buan/network/protocol_parser.hpp"
#include "buan/core/ring_buffer.hpp"
#include "buan/core/types.hpp"
#include "buan/risk/risk_gate.hpp"
#include "buan/util/vol_surface.hpp"
#include "buan/core/symbol_index.hpp"
#include "buan/core/market_state_arena.hpp"
#include <expected>
#include <memory>

namespace buan {

/**
 * @enum EngineStatus
 * @brief High-resolution status codes for the atomic polling loop.
 */
enum class EngineStatus {
    IDLE,
    SIGNAL_CAPTURED,
    FILTERED_BY_BREAKER,
    FILTERED_BY_RISK,
    BUFFER_FULL,
    PORTAL_EMPTY
};

/**
 * @class BuanEngine
 * @brief The Monadic Orchestrator.
 */
class BuanEngine {
private:
    IPortal& m_portal;
    BuanRingBuffer<BuanDescriptor, 1024>& m_ring;
    BuanRingBuffer<BuanAuditDescriptor, 8192>& m_audit_ring;
    BuanRiskEngine& m_risk_gate;
    // Task 3.2 Support: The Volatility Surface for pricing
    std::unique_ptr<VolSurface> m_vol_surface;

    SymbolIndex m_symbol_index; 
    MarketStateArena m_market_arena; // The Shared Knowledge Base

    // Task 1.1 Support: Reference to the XDP portal for egress
    BuanXDPPortal* m_xdp_portal{nullptr};

    // Thresholds for the Multi-Check Circuit Breaker
    float    m_drift_threshold = 0.5f;
    int64_t  m_price_spike_limit = 1000000;
    uint32_t m_max_vol_limit = 50000;

public:
    BuanEngine(IPortal& portal, BuanRingBuffer<BuanDescriptor, 1024>& ring, BuanRingBuffer<BuanAuditDescriptor, 8192>& audit_ring, BuanRiskEngine& risk_gate)
        : m_portal(portal), m_ring(ring), m_audit_ring(audit_ring), m_risk_gate(risk_gate) {
            m_vol_surface = std::make_unique<VolSurface>();
        }
    /**
     * @brief Executes a single "Atomic Step" in the pipeline.
     */
    [[nodiscard]] __attribute__((always_inline))
    auto step() noexcept -> EngineStatus;

    void set_threshold(float val) { m_drift_threshold = val; }
    void set_price_spike_limit(int64_t val) { m_price_spike_limit = val; }
    void set_max_vol_limit(uint32_t val) { m_max_vol_limit = val; }
    void set_xdp_portal(BuanXDPPortal* portal) { m_xdp_portal = portal; }
    [[nodiscard]] MarketState* get_arena_raw_ptr() noexcept { return m_market_arena.raw_ptr(); }
    [[nodiscard]] size_t get_arena_size() const noexcept { return 2048; } // Max Tickers
};

} // namespace buan