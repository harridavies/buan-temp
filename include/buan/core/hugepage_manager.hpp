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

    auto allocate() -> std::expected<void*, MemoryError> {
        if (numa_available() < 0) {
            m_ptr = std::aligned_alloc(HP_SIZE, m_total_size);
            m_is_numa_managed = false;
        } else {
            m_ptr = numa_alloc_onnode(m_total_size, m_node);
            if (!m_ptr) return std::unexpected(MemoryError::ALLOCATION_FAILED);
            m_is_numa_managed = true;

            unsigned long nodemask = (1UL << m_node);
            if (set_mempolicy(MPOL_BIND, &nodemask, sizeof(nodemask) * 8 + 1) != 0) {
                return std::unexpected(MemoryError::BIND_POLICY_FAILED);
            }
            // Phase 6: Hard-lock the memory pages into the RAM to prevent swap-out
            if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
                return std::unexpected(MemoryError::LOCK_FAILED);
            }
        }

        int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_LOCKED | MAP_POPULATE;
        void* h_ptr = mmap(nullptr, m_total_size, PROT_READ | PROT_WRITE, flags, -1, 0);

        if (h_ptr == MAP_FAILED) {
            if (mlock(m_ptr, m_total_size) != 0) return std::unexpected(MemoryError::LOCK_FAILED);
            return m_ptr;
        }

        // Clean up initial numa buffer and use the HugePage mapping
        if (m_is_numa_managed) numa_free(m_ptr, m_total_size);
        else std::free(m_ptr);

        m_ptr = h_ptr;
        m_is_mmaped = true;
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