#pragma once

#include "buan/network/portal_interface.hpp"
#include <string>
#include <expected>
#include <cstdint>
#include <xdp/xsk.h>

namespace buan {

class BuanXDPPortal : public IPortal {
private:
    std::string m_ifname;
    uint32_t m_queue_id;
    struct xsk_socket* m_xsk{nullptr};
    struct xsk_umem* m_umem{nullptr};
    
    // Core memory tracking
    void* m_umem_area{nullptr};
    size_t m_umem_size{0};

    // AF_XDP Rings
    struct xsk_ring_prod m_fill_ring;
    struct xsk_ring_cons m_comp_ring;
    struct xsk_ring_cons m_rx_ring;
    struct xsk_ring_prod m_tx_ring;

public:
    explicit BuanXDPPortal(std::string ifname, uint32_t queue_id = 0);
    ~BuanXDPPortal();

    auto map_memory_region(void* buffer, size_t size) -> std::expected<void, PortalError>;
    auto open() -> std::expected<void, PortalError>;
    
    [[nodiscard]] __attribute__((always_inline)) 
    auto poll_frame() noexcept -> std::expected<IngestFrame, PortalError> override;
    
    void release_frame(void* addr) noexcept override;

    /**
     * @brief Task 1.1: Reclaims completed TX descriptors from the NIC.
     */
    void complete_tx() noexcept;
    
    // Non-copyable for hardware stability
    BuanXDPPortal(const BuanXDPPortal&) = delete;
    auto operator=(const BuanXDPPortal&) -> BuanXDPPortal& = delete;

    /**
     * @brief Task 7.3.1: Submits an order template directly to the NIC TX ring.
     */
    [[nodiscard]] auto send_order(void* addr, uint32_t len) noexcept -> std::expected<void, PortalError>;
};

} // namespace buan