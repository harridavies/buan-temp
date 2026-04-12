#include "buan/network/xdp_portal.hpp"
#include <xdp/libxdp.h>
#include <net/if.h>
#include <sys/mman.h>
#include <linux/if_link.h>

/* Fallback for SKB mode flag if libxdp headers are older/different */
#ifndef XDP_FLAGS_SKB_MODE
#define XDP_FLAGS_SKB_MODE (1U << 1)
#endif

namespace buan {

BuanXDPPortal::BuanXDPPortal(std::string ifname, uint32_t queue_id)
    : m_ifname(std::move(ifname)), m_queue_id(queue_id) {}

BuanXDPPortal::~BuanXDPPortal() {
    if (m_xsk) xsk_socket__delete(m_xsk);
    if (m_umem) xsk_umem__delete(m_umem);
}

auto BuanXDPPortal::map_memory_region(void* buffer, size_t size) -> std::expected<void, PortalError> {
    m_umem_area = buffer;
    m_umem_size = size;

    struct xsk_umem_config cfg = {
        .fill_size = 2048,
        .comp_size = 2048,
        .frame_size = 2048,
        .frame_headroom = 0,
        .flags = 0
    };

    int ret = xsk_umem__create(&m_umem, m_umem_area, m_umem_size, &m_fill_ring, &m_comp_ring, &cfg);
    if (ret != 0) return std::unexpected(PortalError::UMEM_MAP_FAILED);

    return {};
}

auto BuanXDPPortal::open() -> std::expected<void, PortalError> {
    struct xsk_socket_config cfg = {
        .rx_size = 2048,
        .tx_size = 2048,
        .libxdp_flags = XDP_FLAGS_SKB_MODE, // Required for Virtualized/OrbStack NICs
        .xdp_flags = 0,
        .bind_flags = XDP_USE_NEED_WAKEUP
    };

    int ret = xsk_socket__create(&m_xsk, m_ifname.c_str(), m_queue_id, m_umem, &m_rx_ring, &m_tx_ring, &cfg);
    if (ret != 0) return std::unexpected(PortalError::SOCKET_CREATE_FAILED);

    return {};
}

auto BuanXDPPortal::poll_frame() noexcept -> std::expected<IngestFrame, PortalError> {
    uint32_t idx;
    if (xsk_ring_cons__peek(&m_rx_ring, 1, &idx) == 0) {
        return std::unexpected(PortalError::EMPTY);
    }

    const struct xdp_desc* desc = xsk_ring_cons__rx_desc(&m_rx_ring, idx);
    void* pkt_addr = xsk_umem__get_data(m_umem_area, desc->addr);
    
    IngestFrame frame;
    frame.addr = pkt_addr;
    frame.len = desc->len;
    
    xsk_ring_cons__release(&m_rx_ring, 1);
    return frame;
}

void BuanXDPPortal::release_frame(void* addr) noexcept {
    (void)addr;
}

} // namespace buan