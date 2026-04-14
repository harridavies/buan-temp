#pragma once

#include <cstdint>
#include <cstring>

namespace buan {

/**
 * @struct NewOrderSingle
 * @brief SBE-compliant binary layout for outbound orders.
 * * Use [[gnu::packed]] to prevent ARM64 padding from exceeding 64 bytes.
 */
struct [[gnu::packed]] alignas(64) NewOrderSingle {
    // 0-23: Hot Path (Naturally 8-byte aligned)
    uint64_t cl_ord_id;    
    int64_t  price;        
    uint64_t quantity;     

    // 24-33: Warm Path (Naturally aligned)
    uint32_t symbol_id;    
    uint16_t template_id;  
    uint16_t schema_id;    
    uint16_t version;      

    // 34-63: Metadata (Byte-aligned)
    uint8_t  side;         
    char     account[12];   
    char     sender_id[12]; 
    uint8_t  reserved[5];   
};

static_assert(sizeof(NewOrderSingle) == 64, "NewOrderSingle must be exactly 64 bytes for cache isolation.");

class SbeOrderBuilder {
public:
    static void prepare_template(NewOrderSingle* nos, uint32_t symbol, const char* acc, const char* sender) {
        std::memset(nos, 0, sizeof(NewOrderSingle));
        nos->symbol_id = symbol;
        nos->template_id = 101;
        nos->schema_id = 1;
        nos->version = 1;
        std::strncpy(nos->account, acc, 11);
        nos->account[11] = '\0';
        std::strncpy(nos->sender_id, sender, 11);
        nos->sender_id[11] = '\0';
    }

    static inline void patch(NewOrderSingle* nos, uint64_t cl_id, int64_t px, uint64_t qty, uint8_t s) {
        nos->cl_ord_id = cl_id;
        nos->price = px;
        nos->quantity = qty;
        nos->side = s;
    }
};

} // namespace buan