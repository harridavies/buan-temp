#define PCAP_DONT_INCLUDE_PCAP_BPF
#include "buan/network/pcap_portal.hpp"

namespace buan {

BuanPcapPortal::BuanPcapPortal(const std::string& filename) {
    char errbuf[PCAP_ERRBUF_SIZE];
    m_handle = pcap_open_offline(filename.c_str(), errbuf);
}

BuanPcapPortal::~BuanPcapPortal() {
    if (m_handle) pcap_close(m_handle);
}

auto BuanPcapPortal::poll_frame() noexcept -> std::expected<IngestFrame, PortalError> {
    struct pcap_pkthdr* header;
    const u_char* data;

    int res = pcap_next_ex(m_handle, &header, &data);
    if (res != 1) return std::unexpected(PortalError::EMPTY);

    // In a real SBE harness, we would copy this to the HugePage Arena.
    // For now, we return the direct pcap pointer.
    IngestFrame frame;
    frame.addr = const_cast<u_char*>(data);
    frame.len = header->len;
    return frame;
}

void BuanPcapPortal::release_frame(void* addr) noexcept { (void)addr; }

} // namespace buan