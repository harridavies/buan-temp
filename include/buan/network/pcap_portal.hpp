#pragma once

// Fix for Task 10.3: Prevent redefinition of bpf_insn between pcap and linux/bpf.h
#ifndef PCAP_DONT_INCLUDE_PCAP_BPF_H
#define PCAP_DONT_INCLUDE_PCAP_BPF_H
#endif

#include "buan/network/portal_interface.hpp"
#include <pcap.h>
#include <vector>
#include <string>

namespace buan {

class BuanPcapPortal : public IPortal {
private:
    pcap_t* m_handle{nullptr};
    std::vector<uint8_t> m_buffer; 

public:
    explicit BuanPcapPortal(const std::string& filename);
    ~BuanPcapPortal() override;

    auto poll_frame() noexcept -> std::expected<IngestFrame, PortalError> override;
    void release_frame(void* addr) noexcept override;
};

} // namespace buan