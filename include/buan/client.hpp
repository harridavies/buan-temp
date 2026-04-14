#pragma once

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include "buan/core/market_state_arena.hpp"

namespace buan {

/**
 * @class BuanClient
 * @brief The "Stealth" Entry Point.
 * * Allows any external process to link to the BuanAlpha Arena 
 * with zero-copy overhead.
 */
class BuanClient {
public:
    static auto attach(const std::string& name = "/buan_market_arena") -> const MarketState* {
        size_t size = 2048 * sizeof(MarketState);
        int fd = shm_open(name.c_str(), O_RDONLY, 0666);
        if (fd < 0) return nullptr;

        void* ptr = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
        close(fd);

        if (ptr == MAP_FAILED) return nullptr;
        return static_cast<const MarketState*>(ptr);
    }
};

} // namespace buan