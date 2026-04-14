#pragma once

#include <atomic>
#include <string>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include "buan/core/ring_buffer.hpp"
#include "buan/core/types.hpp"
#include "buan/util/affinity_helper.hpp"

namespace buan {

/**
 * @class AuditFlusher
 * @brief Task 10.2: Background flusher for the Shadow Log.
 * * Uses mmap and msync to ensure zero-impact on the Hot Path trading core.
 */
class AuditFlusher {
private:
    BuanRingBuffer<BuanAuditDescriptor, 8192>& m_ring;
    std::atomic<bool>& m_running;
    std::string m_filename;
    static constexpr size_t FILE_SIZE = 1024 * 1024 * 100; // 100MB pre-allocated log

public:
    AuditFlusher(BuanRingBuffer<BuanAuditDescriptor, 8192>& ring, 
                 std::atomic<bool>& running,
                 std::string filename)
        : m_ring(ring), m_running(running), m_filename(std::move(filename)) {}

    void run() {
        // Task 10.2.1: Pin to Core 0 to avoid jitter on trading cores
        static_cast<void>(pin_thread(0));

        int fd = open(m_filename.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) return;
        
        if (ftruncate(fd, FILE_SIZE) != 0) {
            close(fd);
            return;
        }

        void* map = mmap(nullptr, FILE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED) {
            close(fd);
            return;
        }

        uint8_t* current = static_cast<uint8_t*>(map);
        size_t bytes_written = 0;
        BuanAuditDescriptor desc;

        while (m_running.load(std::memory_order_acquire)) {
            bool found = false;
            while (m_ring.pop(desc)) {
                if (bytes_written + sizeof(desc) > FILE_SIZE) break;
                
                std::memcpy(current, &desc, sizeof(desc));
                current += sizeof(desc);
                bytes_written += sizeof(desc);
                found = true;
            }

            if (found) {
                // Task 10.2.2: Async flush to NVMe
                msync(map, bytes_written, MS_ASYNC);
            }
            
            // Back-off for 10ms to keep Core 0 power-efficient
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        munmap(map, FILE_SIZE);
        close(fd);
    }
};

} // namespace buan