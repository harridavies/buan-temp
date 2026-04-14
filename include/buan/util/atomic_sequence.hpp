#pragma once

#include <atomic>
#include <cstdint>

namespace buan {

/**
 * @class AtomicSequence
 * @brief High-performance sequence provider for Exchange Protocols.
 * * Uses alignas(64) to prevent "False Sharing" if multiple trading 
 * pods are accessing different sequence managers on the same CPU socket.
 */
class alignas(64) AtomicSequence {
private:
    std::atomic<uint64_t> m_next_seq;

public:
    explicit AtomicSequence(uint64_t initial = 1) : m_next_seq(initial) {}

    /**
     * @brief Claims the next sequence number.
     * Uses memory_order_relaxed because exchange sequence numbers 
     * usually don't require strict happens-before visibility relative 
     * to other memory, just uniqueness.
     */
    [[nodiscard]] __attribute__((always_inline))
    uint64_t next() noexcept {
        return m_next_seq.fetch_add(1, std::memory_order_relaxed);
    }

    void reset(uint64_t val) noexcept {
        m_next_seq.store(val, std::memory_order_release);
    }
    
    [[nodiscard]] uint64_t current() const noexcept {
        return m_next_seq.load(std::memory_order_relaxed);
    }
};

} // namespace buan