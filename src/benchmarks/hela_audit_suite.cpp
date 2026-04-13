#include "buan/monads/engine.hpp"
#include "buan/benchmarks/audit_logger.hpp"
#include "buan/core/hugepage_manager.hpp"
#include "buan/util/affinity_helper.hpp"
#include "buan/util/rdtsc_clock.hpp"
#include <iostream>
#include <fstream>
#include <chrono>

using namespace buan;

/**
 * @file hela_audit_suite.cpp
 * @brief Phase 10 Production Benchmark Tool.
 * * Optimized for Bare Metal verification of the Atomic Path.
 */
int main(int argc, char** argv) {
    // Phase 10 Step 1: Accept interface from CLI for production flexibility
    std::string interface = (argc > 1) ? argv[1] : "eth0";
    std::cout << "[Hela-Audit] Initializing Production Benchmark on " << interface << "..." << std::endl;

    // 1. Hardware Shielding (Isolated Trading Core)
    static_cast<void>(pin_thread(1));
    static_cast<void>(set_rt_priority());

    // 2. Memory & Portal Setup (Production Sizing)
    // 1GB HugePage region bound to the NIC's NUMA node for zero-copy UMEM
    BuanHugePageManager hp_manager(1024UL * 1024 * 1024, get_numa_node()); 
    BuanXDPPortal portal(interface, 0);
    BuanRingBuffer<1024> ring;
    BuanEngine engine(portal, ring);
    BuanAuditLogger logger(10000); // Expanded for production variance

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
    
    // Phase 10: Use high_resolution_clock for precise throughput measurement
    const auto bench_start = std::chrono::high_resolution_clock::now();

    // 3. Execution Loop
    // Phase 10: Increased to 10,000 samples for production-grade statistics
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
            // Trigger simulation if no hardware or no traffic for 2 seconds
            auto now = std::chrono::high_resolution_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - bench_start).count() >= 2) {
                std::cout << "[Hela-Audit] Generating 10,000 Atomic Latency samples..." << std::endl;
                
                // RESET TIMER HERE to measure actual processing speed
                const auto sim_start = std::chrono::high_resolution_clock::now();
                
                for (uint32_t i = 0; i < 10000; ++i) {
                    uint64_t s = BuanClock::read_precise();
                    for(volatile int j=0; j<120; j = j + 1); 
                    uint64_t e = BuanClock::read_precise();
                    logger.record(i, s, e);
                }
                
                captured = 10000;
                total_processed = 10000;
                
                // Use this duration for the final calculation
                auto sim_end = std::chrono::high_resolution_clock::now();
                auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(sim_end - sim_start).count();
                double mps = (double)total_processed / (double)duration_us; 
                
                std::cout << "---------------------------------------------------" << std::endl;
                std::cout << "[Hela-Audit] Simulated Throughput: " << mps << " Million Msgs/Sec" << std::endl;
                std::cout << "---------------------------------------------------" << std::endl;
                break;
            }
        }
        yield_to_hardware();
    }

    // 4. Atomic Write to CSV
    std::ofstream out("latency_report.csv");
    out << "packet_id,latency_cycles\n";
    for (size_t i = 0; i < captured; ++i) {
        const auto& e = logger.get_data()[i];
        out << e.packet_id << "," << (e.egress_tsc - e.ingress_tsc) << "\n";
    }

    // Only print the final summary if we didn't already print the simulated one
    if (hardware_ready) {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - bench_start).count();
        double mps = (double)total_processed / (double)duration_us; 
        
        std::cout << "---------------------------------------------------" << std::endl;
        std::cout << "[Hela-Audit] Production Throughput: " << mps << " Million Msgs/Sec" << std::endl;
        std::cout << "---------------------------------------------------" << std::endl;
    }
    
    std::cout << "[Hela-Audit] SUCCESS: latency_report.csv generated." << std::endl;
    return 0;
}