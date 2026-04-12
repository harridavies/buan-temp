#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>

#include "buan/network/xdp_portal.hpp"
#include "buan/core/hugepage_manager.hpp"
#include "buan/monads/engine.hpp"
#include "buan/util/affinity_helper.hpp"

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

    std::cout << "[BuanAlpha] Initializing Atomic Ingest on " << interface << " (Queue " << queue_id << ")" << std::endl;

    // 2. Resource Isolation (The Moat)
    auto affinity_res = pin_thread(1);
    if (!affinity_res) {
        std::cerr << "[Error] Failed to pin thread to isolated core. Are you running as root?" << std::endl;
        return 1;
    }

    auto rt_res = set_rt_priority();
    if (!rt_res) {
        std::cerr << "[Warning] Failed to set Real-Time priority. Latency jitter may occur." << std::endl;
    }

    // 3. Memory & Network Orchestration
    // Allocate 1GB HugePage region for zero-copy UMEM
    BuanHugePageManager hp_manager(1024UL * 1024 * 1024, get_numa_node());
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
    BuanRingBuffer<1024> ring;
    BuanEngine engine(portal, ring);

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
    return 0;
}