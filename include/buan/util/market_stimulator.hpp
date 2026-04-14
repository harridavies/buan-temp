#pragma once

#include <atomic>
#include <thread>
#include <vector>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "buan/network/protocol_parser.hpp"

namespace buan {

/**
 * @class MarketStimulator
 * @brief High-velocity packet generator for stress-testing.
 */
class MarketStimulator {
private:
    struct SimulatedPacket {
        BuanParser::CmeBinaryHeader binary;
        BuanParser::SbeMessageHeader sbe;
        BuanParser::CmeTradeReport trade;
    } __attribute__((packed));

    std::atomic<bool> m_running{false};
    std::vector<std::thread> m_workers;
    const std::string m_target_ip;
    const uint16_t m_target_port;

    // Fast Xorshift RNG for high-entropy price generation
    static uint32_t xorshift32(uint32_t& state) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

public:
    MarketStimulator(std::string ip, uint16_t port) 
        : m_target_ip(std::move(ip)), m_target_port(port) {}

    void stop() {
        m_running.store(false);
        for (auto& t : m_workers) if (t.joinable()) t.join();
    }

    void start(int num_threads = 4) {
        m_running.store(true);
        for (int i = 0; i < num_threads; ++i) {
            m_workers.emplace_back([this, i]() {
                int fd = socket(AF_INET, SOCK_DGRAM, 0);
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(m_target_port);
                addr.sin_addr.s_addr = inet_addr(m_target_ip.c_str());

                uint32_t rng_state = 12345 + i;
                SimulatedPacket pkt{};
                
                // Pre-bake static SBE headers
                pkt.sbe.template_id = 42; // Trade Report
                pkt.sbe.block_length = sizeof(BuanParser::CmeTradeReport);

                while (m_running.load(std::memory_order_relaxed)) {
                    // Generate simulated data for 1,000 symbols
                    pkt.trade.security_id = xorshift32(rng_state) % 1000;
                    pkt.trade.md_entry_px = 50000 + (xorshift32(rng_state) % 1000);
                    pkt.trade.md_entry_size = 1 + (xorshift32(rng_state) % 100);
                    
                    // Fire-and-forget
                    sendto(fd, &pkt, sizeof(pkt), 0, (struct sockaddr*)&addr, sizeof(addr));
                }
                close(fd);
            });
        }
    }
};

} // namespace buan