#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>

#include "buan/network/xdp_portal.hpp"
#include "buan/core/hugepage_manager.hpp"
#include "buan/monads/engine.hpp"
#include "buan/util/affinity_helper.hpp"
#include "buan/util/market_stimulator.hpp"
#include "buan/benchmarks/audit_flusher.hpp"
#include "buan/network/sbe_order_builder.hpp"

using namespace buan;

// Global flag for graceful shutdown via SIGINT
std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running.store(false, std::memory_order_release);
}

int main(int argc, char** argv) {
    // 1. Setup Signal Handling
    std::signal(SIGINT, signal_handler);

    std::string interface = (argc > 1) ? argv[1] : "eth0";
    uint32_t queue_id = (argc > 2) ? std::stoul(argv[2]) : 0;

    if (argc < 2) {
        std::cout << "Usage: sudo ./buan_engine [interface] [queue_id] [core_id]" << std::endl;
    }
    
    std::cout << "[BuanAlpha] Initializing Atomic Ingest on " << interface << " (Queue " << queue_id << ")" << std::endl;

    // 2. Resource Isolation (The Moat)
    int target_core = (argc > 3) ? std::stoi(argv[3]) : 1;
    auto affinity_res = pin_thread(target_core);

    if (!affinity_res) {
        std::cerr << "[Error] Failed to pin thread to isolated core. Are you running as root?" << std::endl;
        return 1;
    }

    // Align memory to the NIC's physical NUMA socket
    int nic_node = get_nic_numa_node(interface);
    std::cout << "[BuanAlpha] Hardware Alignment: NIC on NUMA Node " << nic_node << std::endl;

    auto rt_res = set_rt_priority();
    if (!rt_res) {
        std::cerr << "[Warning] Failed to set Real-Time priority. Latency jitter may occur." << std::endl;
    }

    // 3. Memory & Network Orchestration
    // Allocate 1GB HugePage region on the same NUMA node as the NIC
    BuanHugePageManager hp_manager(1024UL * 1024 * 1024, nic_node);
    auto alloc_res = hp_manager.allocate();
    if (!alloc_res) {
        std::cerr << "[Error] HugePage allocation failed. Run scripts/setup_env.sh first." << std::endl;
        return 1;
    }

    BuanXDPPortal portal(interface, queue_id);
    auto map_res = portal.map_memory_region(hp_manager.data(), hp_manager.size());
    if (!map_res) {
        std::cerr << "[Error] Failed to map HugePage memory to XDP UMEM." << std::endl;
        return 1;
    }

    auto open_res = portal.open();
    if (!open_res) {
        std::cerr << "[Error] Failed to open XDP socket. Check if interface exists." << std::endl;
        return 1;
    }

    // 4. Start the Engine
    BuanRingBuffer<BuanDescriptor, 1024> ring;
    BuanRingBuffer<BuanAuditDescriptor, 8192> audit_ring;
    std::cout << "[BuanAlpha] Shadow Log Online (Size: 8192)" << std::endl;

    // Ensure the flusher filename includes a timestamp or ID for uniqueness
    std::string log_name = "buan_audit_" + std::to_string(BuanClock::read()) + ".bin";
    AuditFlusher flusher(audit_ring, g_running, log_name);
    std::thread flusher_thread(&AuditFlusher::run, &flusher);
    std::cout << "[BuanAlpha] Audit Shadow Log writing to: " << log_name << std::endl;

    // Pillar 2: Initialize Risk Engine
    // Params: Max Pos (10k), Max Drift (500 bps / 5%), Max Order Size (5k), Max Rate (1000 msgs/ms)
    BuanRiskEngine risk_gate(10000, 500, 5000, 1000);
    std::cout << "[BuanAlpha] Sovereign Guard ARMED." << std::endl;

    BuanEngine engine(portal, ring, audit_ring, risk_gate);

    // Task 7.1.3: Carve out a "Hot Template" region from the HugePage memory.
    // This memory is already mapped to the UMEM, making it DMA-accessible for the NIC.
    void* template_base = hp_manager.data();
    if (!template_base) {
        std::cerr << "[Error] Could not locate template memory region." << std::endl;
        return 1;
    }

    // Task 7.2.1: Initialize a sample order template for the "Atomic Strike".
    auto* nos_template = static_cast<NewOrderSingle*>(template_base);
    SbeOrderBuilder::pre_bake(nos_template, 1001, "TRADING_01");
    std::cout << "[BuanAlpha] Egress 'Hot Template' initialized at " << template_base << std::endl;

    // Task 10.1: Synchronize with NIC Hardware Clock (eth0 mapping)
    if (BuanClock::sync_with_phc("/dev/ptp0")) {
        std::cout << "[BuanAlpha] PTP Hardware Clock Synchronized." << std::endl;
    } else {
        std::cout << "[BuanAlpha] Warning: PHC sync failed. Check /dev/ptpX." << std::endl;
    }

    // Task 6.2: Optional Stimulator Mode
    bool stim_mode = false;
    for(int i = 1; i < argc; ++i) {
        if(std::string(argv[i]) == "--stimulate") stim_mode = true;
    }

    std::unique_ptr<MarketStimulator> stim;
    if (stim_mode) {
        std::cout << "[BuanAlpha] STIMULATOR MODE: Launching 4 Storm Threads..." << std::endl;
        stim = std::make_unique<MarketStimulator>("192.168.100.2", 12345);
        stim->start(4);
    }

    std::cout << "[BuanAlpha] Engine Online. Busy-polling for signals..." << std::endl;

    // 5. The Hot Path: Infinite Polling Loop
    while (g_running.load(std::memory_order_acquire)) {
        auto status = engine.step();
        
        if (status == EngineStatus::SIGNAL_CAPTURED) {
            // Signal picked up by the monadic pipeline and pushed to the ring buffer.
            // The AI/Consumer thread (Core 2) will pick this up from the RingBuffer.
        }

        // Hint to CPU that we are in a tight polling loop
        yield_to_hardware();
    }

    std::cout << "\n[BuanAlpha] Shutting down gracefully..." << std::endl;
    
    if (flusher_thread.joinable()) flusher_thread.join();

    return 0;
}