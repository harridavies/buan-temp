#pragma once

#include <cstdint>
#include <cstring>
#include <netinet/in.h>

namespace buan {

/**
 * @struct NewOrderSingle
 * @brief Binary-compatible SBE layout for CME/Binance-style egress.
 * * Cache-line aligned (64 bytes) to ensure the NIC doesn't trigger 
 * extra PCI-e reads for a single message.
 */
struct [[gnu::packed]] alignas(64) NewOrderSingle {
    // --- Message Header (12 bytes) ---
    uint16_t msg_size;
    uint16_t block_length;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t version;
    uint16_t seq_num;

    // --- Hot Fields (Targeted by AI) ---
    uint64_t cl_ord_id;    // Client Order ID
    int64_t  price;        // Scaled Fixed-Point
    uint64_t quantity;     // Size

    // --- Metadata/Warm Fields ---
    uint32_t symbol_id;
    uint8_t  side;         // 1=Buy, 2=Sell
    uint8_t  order_type;   // 2=Limit
    uint8_t  time_in_force; // 0=Day, 1=GTC
    char     account[12];
    uint8_t  padding[13];  // Pad to 64 bytes
};

class SbeOrderBuilder {
public:
    /**
     * @brief Pre-bakes constant fields into the HugePage template.
     * Call this during startup only.
     */
    static void pre_bake(NewOrderSingle* nos, uint32_t symbol, const char* acc) {
        std::memset(nos, 0, sizeof(NewOrderSingle));
        nos->msg_size = sizeof(NewOrderSingle);
        nos->block_length = 42; // Example SBE block length
        nos->template_id = 101;
        nos->schema_id = 1;
        nos->version = 1;
        nos->symbol_id = symbol;
        nos->order_type = 2;   // Limit Order
        nos->time_in_force = 1; // GTC
        std::strncpy(nos->account, acc, 11);
    }

    /**
     * @brief The Hot Path Strike.
     * ONLY touches the fields that change per signal.
     */
    static inline void strike(NewOrderSingle* nos, uint64_t oid, int64_t px, uint64_t qty, uint8_t side) {
        nos->cl_ord_id = oid;
        nos->price = px;
        nos->quantity = qty;
        nos->side = side;
        // The NIC sees these changes immediately because 'nos' is in the UMEM.
    }
};

} // namespace buan