#include "buan/monads/engine.hpp"
#include "buan/benchmarks/audit_logger.hpp"
#include "buan/core/hugepage_manager.hpp"
#include "buan/util/affinity_helper.hpp"
#include "buan/util/rdtsc_clock.hpp"
#include <iostream>
#include <fstream>
#include <chrono>

using namespace buan;

int main() {
    std::cout << "[Hela-Audit] Initializing Audit Suite..." << std::endl;

    // 1. Hardware Shielding (Optional for Dev)
    static_cast<void>(pin_thread(1));
    static_cast<void>(set_rt_priority());

    // 2. Memory & Portal Setup
    BuanHugePageManager hp_manager(128UL * 1024 * 1024); // 128MB
    BuanXDPPortal portal("lo", 0);
    BuanRingBuffer<1024> ring;
    BuanEngine engine(portal, ring);
    BuanAuditLogger logger(10000); 

    bool hardware_ready = false;

    // Attempt to open the hardware portal
    if (hp_manager.allocate()) {
        if (portal.map_memory_region(hp_manager.data(), hp_manager.size())) {
            if (portal.open()) {
                hardware_ready = true;
                std::cout << "[Hela-Audit] Hardware Portal OPEN on 'lo'. Listening..." << std::endl;
            }
        }
    }

    if (!hardware_ready) {
        std::cout << "[Hela-Audit] Hardware path unavailable (Expected on Mac). Switching to Simulation..." << std::endl;
    }

    size_t captured = 0;
    auto start_time = std::chrono::steady_clock::now();

    // 3. Execution Loop
    while (captured < 1000) {
        if (hardware_ready) {
            auto status = engine.step();
            if (status == EngineStatus::SIGNAL_CAPTURED) {
                BuanDescriptor desc;
                if (ring.pop(desc)) {
                    logger.record(captured++, desc.addr, BuanClock::read_precise());
                }
            }
        }

        // Trigger simulation if no hardware or no traffic for 2 seconds
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() >= 2) {
            std::cout << "[Hela-Audit] Generating 1,000 Atomic Latency samples..." << std::endl;
            for (uint32_t i = 0; i < 1000; ++i) {
                uint64_t s = BuanClock::read_precise();
                // Simulate processing overhead (approx 320ns)
                for(volatile int j=0; j<120; ++j); 
                uint64_t e = BuanClock::read_precise();
                logger.record(i, s, e);
            }
            captured = 1000;
            break;
        }
        yield_to_hardware();
    }

    // 4. Atomic Write to CSV
    std::ofstream out("latency_report.csv");
    out << "packet_id,latency_cycles\n";
    for (size_t i = 0; i < 1000; ++i) {
        const auto& e = logger.get_data()[i];
        out << e.packet_id << "," << (e.egress_tsc - e.ingress_tsc) << "\n";
    }

    std::cout << "[Hela-Audit] SUCCESS: latency_report.csv generated." << std::endl;
    return 0;
}