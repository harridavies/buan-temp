#define PCAP_DONT_INCLUDE_PCAP_BPF_H
#include "buan/network/pcap_portal.hpp" // Include pcap first to establish the macro guard
#include "buan/monads/engine.hpp"
#include "buan/benchmarks/audit_logger.hpp"
#include "buan/core/hugepage_manager.hpp"
#include "buan/util/affinity_helper.hpp"
#include "buan/util/rdtsc_clock.hpp"
#include "buan/risk/risk_gate.hpp"
#include "buan/benchmarks/audit_flusher.hpp"
#include <atomic>
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>

using namespace buan;

/**
 * @file hela_audit_suite.cpp
 * @brief Phase 10 Production Benchmark Tool.
 * * Optimized for Bare Metal verification of the Atomic Path.
 */
int main(int argc, char** argv) {
    // Accept interface from CLI for production flexibility
    std::string interface = (argc > 1) ? argv[1] : "eth0";
    std::cout << "[Hela-Audit] Initializing Production Benchmark on " << interface << "..." << std::endl;

    // 1. Hardware Shielding (Isolated Trading Core)
    static_cast<void>(pin_thread(1));
    static_cast<void>(set_rt_priority());

    // 2. Memory & Portal Setup (Production Sizing)
    // 1GB HugePage region bound to the NIC's NUMA node for zero-copy UMEM
    BuanHugePageManager hp_manager(1024UL * 1024 * 1024, get_nic_numa_node(interface)); 
    BuanXDPPortal portal(interface, 0);

    // FIXED: Explicit template parameters required for the new RingBuffer
    BuanRingBuffer<BuanDescriptor, 1024> ring;
    BuanRingBuffer<BuanAuditDescriptor, 8192> audit_ring; 

    // Phase 8: Initialize the Risk Gate for production verification
    BuanRiskGate risk_gate(10000, 10000000000LL, 500000000000LL);

    // Engine now takes the risk_gate as the 4th argument
    BuanEngine engine(portal, ring, audit_ring, risk_gate);

    // Phase 9: Paced Replay state variables
    uint64_t last_historical_ns = 0;
    auto last_wall_clock = std::chrono::high_resolution_clock::now();
    
    BuanAuditLogger logger(10000); 

    bool hardware_ready = false;

    // Attempt to open the hardware portal in Zero-Copy mode
    if (hp_manager.allocate()) {
        if (portal.map_memory_region(hp_manager.data(), hp_manager.size())) {
            if (portal.open()) {
                hardware_ready = true;
                std::cout << "[Hela-Audit] Hardware Portal OPEN on " << interface << ". Listening..." << std::endl;
            }
        }
    }

    if (!hardware_ready) {
        std::cout << "[Hela-Audit] Hardware path unavailable (Expected on Mac). Switching to Simulation..." << std::endl;
    }

    size_t captured = 0;
    uint64_t total_processed = 0;
    
    const auto bench_start = std::chrono::high_resolution_clock::now();

    std::atomic<bool> bench_running{true};
    AuditFlusher flusher(audit_ring, bench_running, "bench_audit.bin");
    std::thread flusher_thread(&AuditFlusher::run, &flusher);

    // 3. Execution Loop
    while (captured < 10000) {
        if (hardware_ready) {
            auto status = engine.step();
            total_processed++;
            
            if (status == EngineStatus::SIGNAL_CAPTURED) {
                BuanDescriptor desc;
                if (ring.pop(desc)) {
                    // Capture final Egress TSC at the point of delivery to Python/AI
                    logger.record(captured++, desc.addr, BuanClock::read_precise());
                }
            }
        } else {
            // Task 9.2: Market Replay Simulation Path
            // On MacBook/Orbstack, we use the PcapPortal for high-fidelity backtesting.
            std::string pcap_file = (argc > 2) ? argv[2] : "data/market_sample.pcap";
            BuanPcapPortal pcap_portal(pcap_file);
            
            std::cout << "[Hela-Audit] Starting Paced Replay of: " << pcap_file << std::endl;
            // Simulation start captured for throughput metrics [Task 9.2]
            // Simulation start captured for throughput metrics [Task 9.2]
            const auto sim_start = std::chrono::high_resolution_clock::now();
            (void)sim_start; // Explicitly suppress unused variable warning

            while (captured < 10000) {
                auto frame_res = pcap_portal.poll_frame();
                if (!frame_res) break;

                auto frame = frame_res.value();

                // Task 9.2: ENFORCE PACING
                if (last_historical_ns > 0) {
                    uint64_t time_delta_ns = frame.historical_ns - last_historical_ns;
                    
                    // Enforce gap if it's larger than 1 microsecond to avoid OS jitter
                    if (time_delta_ns > 1000) {
                        auto target_time = last_wall_clock + std::chrono::nanoseconds(time_delta_ns);
                        std::this_thread::sleep_until(target_time);
                    }
                }

                // Inject frame into the engine
                // (Assuming you add a BuanEngine::inject(frame) or similar)
                // For now, we simulate the engine step timing:
                uint64_t start_tsc = BuanClock::read_precise();
                
                // Update pacing state
                last_historical_ns = frame.historical_ns;
                last_wall_clock = std::chrono::high_resolution_clock::now();

                // Record the "Atomic Gap"
                logger.record(captured++, start_tsc, BuanClock::read_precise());
                total_processed++;
            }
            break; 
        }

        // Simplified Mac compatibility: yield_to_hardware() might not exist on all platforms
        // You can keep your yield_to_hardware() call here if defined in your util headers.
    }

    // 4. Atomic Write to CSV (Updated for CAR 2026 Format)
    std::ofstream out("latency_report.csv");
    out << BuanAuditLogger::get_compliance_header();
    for (size_t i = 0; i < captured; ++i) {
        const auto& e = logger.get_data()[i];
        uint64_t delta_ns = (e.egress_tsc - e.ingress_tsc);
        out << e.packet_id << "," << e.ingress_tsc << "," << e.egress_tsc << "," << delta_ns << "\n";
    }

    if (hardware_ready) {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - bench_start).count();
        double mps = (double)total_processed / (double)duration_us; 
        
        std::cout << "---------------------------------------------------" << std::endl;
        std::cout << "[Hela-Audit] Production Throughput: " << mps << " Million Msgs/Sec" << std::endl;
        std::cout << "---------------------------------------------------" << std::endl;
    }
    
    bench_running.store(false);
    if (flusher_thread.joinable()) flusher_thread.join();

    std::cout << "[Hela-Audit] SUCCESS: latency_report.csv generated." << std::endl;
    return 0;
}