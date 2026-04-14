#pragma once

#include "buan/network/portal_interface.hpp"
#include "buan/network/xdp_portal.hpp"
#include "buan/network/protocol_parser.hpp"
#include "buan/core/ring_buffer.hpp"
#include "buan/core/types.hpp"
#include "buan/risk/risk_gate.hpp"
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
    BuanRiskGate& m_risk_gate;

    // Thresholds for the Multi-Check Circuit Breaker
    float    m_drift_threshold = 0.5f;
    int64_t  m_price_spike_limit = 1000000;
    uint32_t m_max_vol_limit = 50000;

public:
    BuanEngine(IPortal& portal, BuanRingBuffer<BuanDescriptor, 1024>& ring, BuanRingBuffer<BuanAuditDescriptor, 8192>& audit_ring, BuanRiskGate& risk_gate)
        : m_portal(portal), m_ring(ring), m_audit_ring(audit_ring), m_risk_gate(risk_gate) {}
    /**
     * @brief Executes a single "Atomic Step" in the pipeline.
     */
    [[nodiscard]] __attribute__((always_inline))
    auto step() noexcept -> EngineStatus;

    void set_threshold(float val) { m_drift_threshold = val; }
    void set_price_spike_limit(int64_t val) { m_price_spike_limit = val; }
    void set_max_vol_limit(uint32_t val) { m_max_vol_limit = val; }
};

} // namespace buan