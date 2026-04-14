#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>

#include "buan/network/xdp_portal.hpp"
#include "buan/core/hugepage_manager.hpp"
#include "buan/monads/engine.hpp"
#include "buan/util/affinity_helper.hpp"
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

    // Task 8.1 & 8.2: Initialize the Risk Gate
    // Limits: 10,000 unit position, Price Corridor: 100.00 to 5000.00 (scaled by 10^8)
    BuanRiskGate risk_gate(10000, 10000000000LL, 500000000000LL);
    std::cout << "[BuanAlpha] Risk Gate Armed (Net Limit: 10,000)" << std::endl;

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
    SbeOrderBuilder::prepare_template(nos_template, 1001, "TRADING_01", "BUAN_CORE");
    std::cout << "[BuanAlpha] Egress 'Hot Template' initialized at " << template_base << std::endl;

    // Task 10.1: Synchronize with NIC Hardware Clock (eth0 mapping)
    if (BuanClock::sync_with_phc("/dev/ptp0")) {
        std::cout << "[BuanAlpha] PTP Hardware Clock Synchronized." << std::endl;
    } else {
        std::cout << "[BuanAlpha] Warning: PHC sync failed. Check /dev/ptpX." << std::endl;
    }

    // Task 10.2: Start Lock-Free Shadow Log Flusher on Core 0
    AuditFlusher flusher(audit_ring, g_running, "shadow_log.bin");
    std::thread flusher_thread(&AuditFlusher::run, &flusher);
    std::cout << "[BuanAlpha] Shadow Log persistence active on Core 0." << std::endl;

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