#pragma once

#include <cstdint>
#include <type_traits>

namespace buan {

/**
 * @brief Internal index for the Global Market Arena (0-2047).
 */
using InternalSymbolID = uint16_t;

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
    InternalSymbolID internal_id; // Mapping to the 2048-slot Arena
    
    // Total used: 8 (tsc) + 4 (sid) + 4 (vol) + 8 (px) + 4 (drift) + 4 (flags) + 2 (id) = 34 bytes
    // Padding needed: 64 - 34 = 30 bytes
    uint8_t  padding[30];
};

/**
 * @struct BuanAuditDescriptor
 * @brief Zero-overhead Audit Entry.
 * * Used for the Shadow Log to satisfy CAR 2026 requirements.
 */
struct alignas(32) BuanAuditDescriptor {
    uint64_t ingress_tsc;
    uint64_t factor_tsc;  // Captured after Feature Factory finishes
    uint32_t symbol_id;
    uint32_t flags;
    uint8_t  padding[8];  // Pad to 32 bytes
};

static_assert(sizeof(BuanMarketTick) == 64, "BuanMarketTick must be exactly 64 bytes.");
static_assert(std::is_standard_layout_v<BuanMarketTick>, "Must be standard layout for zero-copy binary mapping.");

} // namespace buan