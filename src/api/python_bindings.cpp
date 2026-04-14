#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/optional.h>  // Added for std::optional support
#include <nanobind/ndarray.h>
#include <optional>                 // Added for std::optional
#include "buan/monads/engine.hpp"
#include "buan/core/types.hpp"
#include "buan/core/market_state_arena.hpp"

namespace nb = nanobind;

/**
 * @brief BuanAlpha Python Bindings
 * * This bridge allows Data Scientists to access the Atomic Path 
 * directly from Python. It focuses on zero-copy access to the 
 * ring buffer to ensure the "Bribe" is visible in real-time.
 */
NB_MODULE(buan_alpha, m) {
    
    // 0. Expose the Descriptor (The bridge unit)
    nb::class_<buan::BuanDescriptor>(m, "Descriptor")
        .def_ro("addr", &buan::BuanDescriptor::addr)
        .def_ro("len", &buan::BuanDescriptor::len)
        .def_ro("flags", &buan::BuanDescriptor::flags);

    // 0.5 Expose the RingBuffer pop method
    nb::class_<buan::BuanRingBuffer<buan::BuanDescriptor, 1024>>(m, "RingBuffer")
        .def(nb::init<>()) 
        .def("pop", [](buan::BuanRingBuffer<buan::BuanDescriptor, 1024>& rb) -> std::optional<buan::BuanDescriptor> {
            buan::BuanDescriptor desc;
            if (rb.pop(desc)) return desc;
            return std::nullopt;
        }, nb::call_guard<nb::gil_scoped_release>())
        .def_prop_ro("dropped_count", &buan::BuanRingBuffer<buan::BuanDescriptor, 1024>::dropped_count);

    // 1. Expose the Status Codes
    nb::enum_<buan::EngineStatus>(m, "EngineStatus")
        .value("IDLE", buan::EngineStatus::IDLE)
        .value("SIGNAL_CAPTURED", buan::EngineStatus::SIGNAL_CAPTURED)
        .value("FILTERED_BY_BREAKER", buan::EngineStatus::FILTERED_BY_BREAKER)
        .value("BUFFER_FULL", buan::EngineStatus::BUFFER_FULL)
        .value("PORTAL_EMPTY", buan::EngineStatus::PORTAL_EMPTY);

    // 2. Expose the Market Tick Structure
    // 2. Expose the Market Tick Structure (Updated to Read-Write)
    nb::class_<buan::BuanMarketTick>(m, "MarketTick")
        .def(nb::init<>()) 
        .def_rw("ingress_tsc", &buan::BuanMarketTick::ingress_tsc)
        .def_rw("symbol_id", &buan::BuanMarketTick::symbol_id)
        .def_rw("price", &buan::BuanMarketTick::price)
        .def_rw("volume", &buan::BuanMarketTick::volume)
        .def_rw("signal_drift", &buan::BuanMarketTick::signal_drift)
        .def_rw("flags", &buan::BuanMarketTick::flags); // Added missing flags field
    
    // 2.5 Expose the Global Market State Slot
    nb::class_<buan::MarketState>(m, "MarketState")
        .def_prop_ro("last_price", [](const buan::MarketState& s) { return s.last_price.load(); })
        .def_ro("rolling_z_score", &buan::MarketState::rolling_z_score)
        .def_ro("volatility", &buan::MarketState::volatility)
        .def_ro("rolling_mean", &buan::MarketState::rolling_mean)
        .def_ro("correlation_benchmark", &buan::MarketState::correlation_benchmark);

    // 3. Expose the Engine Orchestrator
    nb::class_<buan::BuanEngine>(m, "Engine")
        .def("step", &buan::BuanEngine::step, nb::call_guard<nb::gil_scoped_release>())
        .def("set_threshold", &buan::BuanEngine::set_threshold)
        .def("set_price_spike_limit", &buan::BuanEngine::set_price_spike_limit)
        .def("set_max_vol_limit", &buan::BuanEngine::set_max_vol_limit) // Removed stray semicolon here
        .def("get_arena_ptr", [](buan::BuanEngine& engine) {
            // This is used for the Zero-Copy shared memory bridge
            return reinterpret_cast<uintptr_t>(engine.get_arena_raw_ptr());
        })
        .def("step_multi", [](buan::BuanEngine& engine, int count) {
            int captured = 0;
            for(int i = 0; i < count; ++i) {
                if(engine.step() == buan::EngineStatus::SIGNAL_CAPTURED) captured++;
            }
            return captured;
        }, nb::call_guard<nb::gil_scoped_release>())
        .def("get_global_market_state", [](buan::BuanEngine& engine) {
            // Map as a raw byte array to avoid template instantiation issues with custom structs
            size_t shape[1] = { engine.get_arena_size() * sizeof(buan::MarketState) };
            return nb::ndarray<nb::numpy, uint8_t>(
                engine.get_arena_raw_ptr(), 1, shape, nb::handle()
            );
        }, nb::rv_policy::reference)
        .def("get_z_score_buffer", [](buan::BuanEngine& engine) {
            // Returns a flat 2,048-float array of current Z-scores
            auto* arena = engine.get_arena_raw_ptr();
            std::vector<float> z_scores(2048);
            for(int i = 0; i < 2048; ++i) {
                z_scores[i] = arena[i].rolling_z_score;
            }
            return nb::ndarray<nb::numpy, float, nb::shape<2048>>(
                z_scores.data(), 1, nullptr, nb::handle()
            );
        }, nb::rv_policy::copy); // We copy here for the 10Hz snapshot to ensure stability

    // 4. The "Secret Weapon": Zero-Copy Tensor Support
    m.def("get_tick_view", [](buan::BuanMarketTick& tick) {
        size_t shape[1] = { sizeof(buan::BuanMarketTick) / sizeof(uint8_t) };
        return nb::ndarray<nb::numpy, uint8_t, nb::shape<-1>>(
            &tick, 1, shape, nb::handle() 
        );
    }, nb::rv_policy::reference);

    m.def("get_tick_ptr", [](const buan::BuanMarketTick& tick) {
        return reinterpret_cast<uintptr_t>(&tick);
    });
}