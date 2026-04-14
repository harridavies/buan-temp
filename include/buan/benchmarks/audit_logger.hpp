#pragma once

#include <vector>
#include <cstdint>
#include <atomic>
#include "buan/core/types.hpp"

namespace buan {

/**
 * @struct AuditEntry
 * @brief Records the lifecycle of a single signal.
 */
struct AuditEntry {
    uint32_t packet_id;
    uint64_t ingress_tsc;
    uint64_t factor_tsc; // Thinking finished
    uint64_t egress_tsc; // Delivery finished
};

/**
 * @class BuanAuditLogger
 * @brief High-speed circular buffer for latency telemetry.
 * * Uses pre-allocated memory to ensure zero heap-allocation during 
 * the measurement window.
 */
class BuanAuditLogger {
private:
    std::vector<AuditEntry> m_entries;
    std::atomic<size_t> m_index{0};
    const size_t m_max_entries;

public:
    explicit BuanAuditLogger(size_t max_entries = 1000000) 
        : m_max_entries(max_entries) {
        m_entries.resize(max_entries);
    }

    /**
     * @brief Records a completed transaction.
     * @param id Packet ID
     * @param ingress TSC captured at NIC
     * @param egress TSC captured at AI delivery
     */
    __attribute__((always_inline))
    void record(uint32_t id, uint64_t ingress, uint64_t factor, uint64_t egress) noexcept {
        size_t idx = m_index.fetch_add(1, std::memory_order_relaxed);
        if (idx < m_max_entries) {
            m_entries[idx] = {id, ingress, factor, egress};
        }
    }

    const std::vector<AuditEntry>& get_data() const { return m_entries; }
    size_t count() const { return std::min(m_index.load(), m_max_entries); }

    /**
     * @brief Generates the CAR 2026 Compliance Header.
     * Includes the Hardware ID and Engine Version for the regulator.
     */
    static auto get_compliance_header() -> std::string {
        return "audit_version: 1.0.2\n"
               "regulator_path: CAR-2026-NANO\n"
               "timestamp_source: INVARIANT_TSC\n"
               "packet_id,ingress_tsc,factor_tsc,egress_tsc,factor_lat_ns,total_lat_ns\n";
    }
};

} // namespace buan