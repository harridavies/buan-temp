#pragma once

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <numa.h>
#include <numaif.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string>
#include <fcntl.h>

namespace buan {

enum class MemoryError {
    NUMA_NOT_AVAILABLE,
    ALLOCATION_FAILED,
    HUGEPAGE_UPGRADE_FAILED,
    LOCK_FAILED,
    BIND_POLICY_FAILED
};

class BuanHugePageManager {
private:
    void* m_ptr = nullptr;
    size_t m_total_size;
    int m_node;
    bool m_is_numa_managed{false};
    bool m_is_mmaped{false};
    std::string m_shm_name; // Name for POSIX Shared Memory

    static constexpr size_t HP_SIZE = 2UL * 1024 * 1024;

public:
    explicit BuanHugePageManager(size_t size, int numa_node = 0)
        : m_total_size(((size + HP_SIZE - 1) / HP_SIZE) * HP_SIZE), m_node(numa_node) {}

    ~BuanHugePageManager() {
        if (m_ptr) {
            if (m_is_mmaped) munmap(m_ptr, m_total_size);
            else if (m_is_numa_managed) numa_free(m_ptr, m_total_size);
            else std::free(m_ptr);
        }
    }

    auto allocate(const std::string& name = "") -> std::expected<void*, MemoryError> {
        m_shm_name = name;
        
        if (!m_shm_name.empty()) {
            // Task 8.1.1: POSIX SHM Orchestration
            int shm_fd = shm_open(m_shm_name.c_str(), O_RDWR | O_CREAT, 0666);
            if (shm_fd < 0) return std::unexpected(MemoryError::ALLOCATION_FAILED);
            
            if (ftruncate(shm_fd, m_total_size) != 0) {
                close(shm_fd);
                return std::unexpected(MemoryError::ALLOCATION_FAILED);
            }

            // Map SHM segment; Use MAP_HUGETLB if on Linux production
            int flags = MAP_SHARED;
#if defined(__linux__)
            flags |= MAP_HUGETLB; 
#endif
            void* ptr = mmap(nullptr, m_total_size, PROT_READ | PROT_WRITE, flags, shm_fd, 0);
            close(shm_fd);

            if (ptr == MAP_FAILED) return std::unexpected(MemoryError::HUGEPAGE_UPGRADE_FAILED);
            m_ptr = ptr;
            m_is_mmaped = true;
            return m_ptr;
        }

        // Fallback to existing anonymous allocation for local tests
        [[maybe_unused]] int current_cpu_node = numa_node_of_cpu(sched_getcpu());
        if (numa_available() < 0) {
            m_ptr = std::aligned_alloc(HP_SIZE, m_total_size);
        } else {
            m_ptr = numa_alloc_onnode(m_total_size, m_node);
        }
        return m_ptr;
    }

    [[nodiscard]] auto data() const noexcept -> void* { return m_ptr; }
    [[nodiscard]] auto size() const noexcept -> size_t { return m_total_size; }
    [[nodiscard]] auto node() const noexcept -> int { return m_node; }
    [[nodiscard]] auto get_device_addr() const noexcept -> uint64_t { return std::bit_cast<uint64_t>(m_ptr); }

    BuanHugePageManager(const BuanHugePageManager&) = delete;
    auto operator=(const BuanHugePageManager&) -> BuanHugePageManager& = delete;
};

} // namespace buan