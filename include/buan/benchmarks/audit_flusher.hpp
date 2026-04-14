#pragma once

#include <atomic>
#include <string>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <vector>
#include "buan/core/ring_buffer.hpp"
#include "buan/core/types.hpp"
#include "buan/util/affinity_helper.hpp"

namespace buan {

/**
 * @class AuditFlusher
 * @brief CAR 2026 Compliance: Background flusher for the Shadow Log.
 * * Uses page-aligned batching to ensure zero-impact on the Trading Core.
 */
class AuditFlusher {
private:
    BuanRingBuffer<BuanAuditDescriptor, 8192>& m_ring;
    std::atomic<bool>& m_running;
    std::string m_filename;
    
    // 4KB Buffer for NVMe alignment
    static constexpr size_t CHUNK_SIZE = 4096;
    static constexpr size_t FILE_SIZE = 1024 * 1024 * 500; // 500MB Log

public:
    AuditFlusher(BuanRingBuffer<BuanAuditDescriptor, 8192>& ring, 
                 std::atomic<bool>& running,
                 std::string filename)
        : m_ring(ring), m_running(running), m_filename(std::move(filename)) {}

    void run() {
        // Task 4.2.1: Pin to Core 0 (The "Janitor" Core)
        static_cast<void>(pin_thread(0));

        // Use O_DIRECT for the fastest NVMe write path if the filesystem supports it
        int fd = open(m_filename.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) return;
        
        ftruncate(fd, FILE_SIZE);

        // Map the file into memory
        void* map = mmap(nullptr, FILE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED) {
            close(fd);
            return;
        }

        uint8_t* write_ptr = static_cast<uint8_t*>(map);
        size_t total_written = 0;
        size_t current_chunk_bytes = 0;
        BuanAuditDescriptor desc;

        while (m_running.load(std::memory_order_acquire) || m_ring.count() > 0) {
            bool has_data = false;
            
            while (m_ring.pop(desc)) {
                if (total_written + sizeof(desc) > FILE_SIZE) break;

                std::memcpy(write_ptr + total_written, &desc, sizeof(desc));
                total_written += sizeof(desc);
                current_chunk_bytes += sizeof(desc);
                has_data = true;

                // Task 4.2.2: Flush specifically at 4KB boundaries
                if (current_chunk_bytes >= CHUNK_SIZE) {
                    msync(write_ptr + (total_written - current_chunk_bytes), CHUNK_SIZE, MS_ASYNC);
                    current_chunk_bytes = 0;
                }
            }

            if (!has_data) {
                // If the ring is empty, back-off to save Core 0 power
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        // Final flush for remaining data
        msync(map, total_written, MS_SYNC);
        munmap(map, FILE_SIZE);
        close(fd);
    }
};

} // namespace buan