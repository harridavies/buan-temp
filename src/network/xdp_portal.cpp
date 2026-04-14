#include "buan/network/xdp_portal.hpp"
#include "buan/util/rdtsc_clock.hpp"
#include <xdp/libxdp.h>
#include <net/if.h>
#include <sys/mman.h>
#include <linux/if_link.h>
#include <linux/if_xdp.h>
#include <netinet/in.h>

/* Fallback for SKB mode flag if libxdp headers are older/different */
#ifndef XDP_FLAGS_SKB_MODE
#define XDP_FLAGS_SKB_MODE (1U << 1)
#endif

namespace buan {

#ifndef XDP_ZERO_COPY
#define XDP_ZERO_COPY (1U << 2)
#endif
#ifndef XDP_USE_NEED_WAKEUP
#define XDP_USE_NEED_WAKEUP (1U << 3)
#endif

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
        .libxdp_flags = 0, 
        .xdp_flags = XDP_FLAGS_DRV_MODE, 
        .bind_flags = static_cast<uint16_t>(XDP_ZERO_COPY | XDP_USE_NEED_WAKEUP)
    };

    int ret = xsk_socket__create(&m_xsk, m_ifname.c_str(), m_queue_id, m_umem, &m_rx_ring, &m_tx_ring, &cfg);
    if (ret != 0) return std::unexpected(PortalError::SOCKET_CREATE_FAILED);

    // Phase 7: Populate Fill Ring so the NIC can start receiving
    uint32_t idx;
    int stock_ret = xsk_ring_prod__reserve(&m_fill_ring, 2048, &idx);
    if (stock_ret > 0) {
        for (uint32_t i = 0; i < 2048; i++) {
            *xsk_ring_prod__fill_addr(&m_fill_ring, idx++) = i * 2048;
        }
        xsk_ring_prod__submit(&m_fill_ring, 2048);
    }
    
    return {};
}

auto BuanXDPPortal::poll_frame() noexcept -> std::expected<IngestFrame, PortalError> {
    const uint64_t ts = BuanClock::read_precise(); // Capture at the earliest visibility

    uint32_t idx;
    if (xsk_ring_cons__peek(&m_rx_ring, 1, &idx) == 0) {
        return std::unexpected(PortalError::EMPTY);
    }

    const struct xdp_desc* desc = xsk_ring_cons__rx_desc(&m_rx_ring, idx);
    void* pkt_addr = xsk_umem__get_data(m_umem_area, desc->addr);
    
    IngestFrame frame;
    frame.addr = pkt_addr;
    frame.len = desc->len;
    frame.ingress_tsc = ts;

    xsk_ring_cons__release(&m_rx_ring, 1);
    return frame;
}

void BuanXDPPortal::release_frame(void* addr) noexcept {
    (void)addr;
}

auto BuanXDPPortal::send_order(void* addr, uint32_t len) noexcept -> std::expected<void, PortalError> {
    uint32_t idx;
    // Reserve a slot in the TX ring for our order.
    if (xsk_ring_prod__reserve(&m_tx_ring, 1, &idx) == 0) {
        return std::unexpected(PortalError::SOCKET_CREATE_FAILED); 
    }

    struct xdp_desc* tx_desc = xsk_ring_prod__tx_desc(&m_tx_ring, idx);
    // AF_XDP requires an offset relative to the start of the UMEM area.
    tx_desc->addr = static_cast<uint8_t*>(addr) - static_cast<uint8_t*>(m_umem_area);
    tx_desc->len = len;

    xsk_ring_prod__submit(&m_tx_ring, 1);

    // Notify the kernel to process the TX ring if the 'need_wakeup' flag is set.
    if (xsk_ring_prod__needs_wakeup(&m_tx_ring)) {
        sendto(xsk_socket__fd(m_xsk), nullptr, 0, MSG_DONTWAIT, nullptr, 0);
    }

    return {};
}

} // namespace buan