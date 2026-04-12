#pragma once

#include <cstdint>
#include <type_traits>

namespace buan {

/**
 * @struct BuanMarketTick
 * @brief 64-Byte Cache-Line Aligned Atomic Signal.
 * * This structure is the "Projectile" of the BuanAlpha engine. 
 * It uses fixed-point math to avoid floating-point jitter and 
 * includes an ingress timestamp for Hela-Audit verification.
 */
struct alignas(64) BuanMarketTick {
    uint64_t ingress_tsc;  // Captured via BuanClock::read_precise()
    uint32_t symbol_id;    // Unique identifier for the data stream
    uint32_t volume;       // Quantity or intensity of signal
    int64_t  price;        // Fixed-point (e.g., multiplied by 1e8)
    
    // Metadata for Circuit Breaker logic
    float    signal_drift; 
    uint32_t flags;
    
    // Padding to ensure the struct is exactly 64 bytes (1 cache line).
    // This prevents "False Sharing" and MESI protocol stalls.
    uint8_t  padding[28]; 
};

static_assert(sizeof(BuanMarketTick) == 64, "BuanMarketTick must be exactly 64 bytes.");
static_assert(std::is_standard_layout_v<BuanMarketTick>, "Must be standard layout for zero-copy binary mapping.");

} // namespace buan