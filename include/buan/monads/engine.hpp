#pragma once

#include "buan/network/portal_interface.hpp"
#include "buan/network/xdp_portal.hpp"
#include "buan/network/protocol_parser.hpp"
#include "buan/core/ring_buffer.hpp"
#include "buan/core/types.hpp"
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
    BUFFER_FULL,
    PORTAL_EMPTY
};

/**
 * @class BuanEngine
 * @brief The Monadic Orchestrator.
 * * This class implements the "Hot Path" logic by composing the 
 * Portal, Parser, and RingBuffer into a zero-branch pipeline.
 */
class BuanEngine {
private:
    IPortal& m_portal;
    BuanRingBuffer<1024>& m_ring;
    float m_drift_threshold = 0.5f;

public:
    BuanEngine(IPortal& portal, BuanRingBuffer<1024>& ring)
        : m_portal(portal), m_ring(ring) {}

    /**
     * @brief Executes a single "Atomic Step" in the pipeline.
     * This is the function the high-priority thread will busy-poll.
     */
    [[nodiscard]] __attribute__((always_inline))
    auto step() noexcept -> EngineStatus;

    void set_threshold(float val) { m_drift_threshold = val; }
};

} // namespace buan