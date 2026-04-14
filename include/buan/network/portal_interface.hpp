#pragma once

#include <expected>
#include <cstdint>

namespace buan {

/**
 * @enum PortalError
 * @brief Error codes for the hardware portal.
 */
enum class PortalError {
    INIT_FAILED,
    UMEM_MAP_FAILED,
    SOCKET_CREATE_FAILED,
    POLL_TIMEOUT,
    NOT_READY,
    EMPTY
};

/**
 * @struct IngestFrame
 * @brief Descriptor for a raw packet in the UMEM.
 */
struct IngestFrame {
    void* addr;
    uint32_t len;
    uint64_t ingress_tsc;     // Local CPU cycles (for live) or Historical NS (for replay)
    uint64_t historical_ns;   // Nanoseconds since epoch from PCAP
};

/**
 * @class IPortal
 * @brief Abstract interface for data ingestion.
 */
class IPortal {
public:
    virtual ~IPortal() = default;

    [[nodiscard]] virtual auto poll_frame() noexcept -> std::expected<IngestFrame, PortalError> = 0;
    virtual void release_frame(void* addr) noexcept = 0;
};

} // namespace buan