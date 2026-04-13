#pragma once

#include "buan/network/portal_interface.hpp"
#include <pcap.h>
#include <vector>
#include <string>

namespace buan {

class BuanPcapPortal : public IPortal {
private:
    pcap_t* m_handle{nullptr};
    std::vector<uint8_t> m_buffer; // Temporary buffer for file reading

public:
    explicit BuanPcapPortal(const std::string& filename);
    ~BuanPcapPortal() override;

    auto poll_frame() noexcept -> std::expected<IngestFrame, PortalError> override;
    void release_frame(void* addr) noexcept override;
};

} // namespace buan