#include "buan/monads/engine.hpp"

namespace buan {

auto BuanEngine::step() noexcept -> EngineStatus {
    // Start the monadic chain by polling the hardware portal
    auto result = m_portal.poll_frame()
        .transform_error([](auto) { return EngineStatus::PORTAL_EMPTY; })
        .and_then([&](IngestFrame frame) -> std::expected<EngineStatus, EngineStatus> {
            /** * WE NEST HERE: 
             * By nesting the next steps inside this lambda, we keep 'frame' 
             * in scope so we can use its memory address at the very end.
             */
            return BuanParser::parse(frame)
                .transform_error([](auto) { return EngineStatus::IDLE; })
                .and_then([&](BuanMarketTick tick) -> std::expected<EngineStatus, EngineStatus> {
                    // 1. Circuit Breaker Logic
                    if (tick.signal_drift > m_drift_threshold) {
                        return std::unexpected(EngineStatus::FILTERED_BY_BREAKER);
                    }
                    
                    // 2. Prepare the Descriptor for the Python RingBuffer
                    // We can now successfully access frame.addr here.
                    BuanDescriptor desc{ 
                        .addr = reinterpret_cast<uint64_t>(frame.addr), 
                        .len = sizeof(BuanMarketTick), 
                        .flags = tick.symbol_id 
                    };
                    
                    // 3. Push to the "Bribe" path
                    if (m_ring.push(desc)) {
                        return EngineStatus::SIGNAL_CAPTURED;
                    }
                    return std::unexpected(EngineStatus::BUFFER_FULL);
                });
        });

    return result.has_value() ? result.value() : result.error();
}

} // namespace buan