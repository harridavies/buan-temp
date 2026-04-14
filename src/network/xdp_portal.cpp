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
    struct xsk_socket_config cfg = {};
    cfg.rx_size = 2048;
    cfg.tx_size = 2048;
    cfg.libxdp_flags = 0;
    cfg.xdp_flags = 0; // Let the library decide the best mode
    cfg.bind_flags = XDP_USE_NEED_WAKEUP;

    // 1. Try High-Performance Native Driver Mode (Production Path)
    cfg.xdp_flags = XDP_FLAGS_DRV_MODE;
    cfg.bind_flags |= XDP_ZERO_COPY;
    
    int ret = xsk_socket__create(&m_xsk, m_ifname.c_str(), m_queue_id, m_umem, &m_rx_ring, &m_tx_ring, &cfg);
    
    // 2. Fallback to Generic/SKB Mode (MacBook/Simulation Path)
    if (ret != 0) {
        cfg.xdp_flags = XDP_FLAGS_SKB_MODE;
        cfg.bind_flags &= ~XDP_ZERO_COPY; // Disable Zero-Copy for virtual interfaces
        cfg.bind_flags |= XDP_COPY;
        
        ret = xsk_socket__create(&m_xsk, m_ifname.c_str(), m_queue_id, m_umem, &m_rx_ring, &m_tx_ring, &cfg);
    }

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

/**
 * @brief Cleans up the Completion Ring.
 * Must be called periodically to free up UMEM frames used by previous TX operations.
 */
void BuanXDPPortal::complete_tx() noexcept {
    uint32_t idx;
    size_t completed = xsk_ring_cons__peek(&m_comp_ring, 2048, &idx);
    if (completed > 0) {
        xsk_ring_cons__release(&m_comp_ring, completed);
    }
}

void BuanXDPPortal::release_frame(void* addr) noexcept {
    (void)addr;
}

auto BuanXDPPortal::send_order(void* addr, uint32_t len) noexcept -> std::expected<void, PortalError> {
    // 1. Maintain ring health: Try to reclaim finished frames first
    complete_tx();

    uint32_t idx;
    // 2. Reserve a slot in the TX ring. Non-blocking.
    if (xsk_ring_prod__reserve(&m_tx_ring, 1, &idx) == 0) {
        return std::unexpected(PortalError::EMPTY); 
    }

    struct xdp_desc* tx_desc = xsk_ring_prod__tx_desc(&m_tx_ring, idx);
    
    // 3. ZERO-COPY: Map the pre-baked template address to UMEM offset
    tx_desc->addr = static_cast<uint8_t*>(addr) - static_cast<uint8_t*>(m_umem_area);
    tx_desc->len = len;

    xsk_ring_prod__submit(&m_tx_ring, 1);

    // 4. Kick the NIC if it's in poll mode
    if (xsk_ring_prod__needs_wakeup(&m_tx_ring)) {
        sendto(xsk_socket__fd(m_xsk), nullptr, 0, MSG_DONTWAIT, nullptr, 0);
    }

    return {};
}

} // namespace buan