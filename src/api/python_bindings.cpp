#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/optional.h>  // Added for std::optional support
#include <nanobind/ndarray.h>
#include <optional>                 // Added for std::optional
#include "buan/monads/engine.hpp"
#include "buan/core/types.hpp"

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
        
    // 3. Expose the Engine Orchestrator
    nb::class_<buan::BuanEngine>(m, "Engine")
        .def("step", &buan::BuanEngine::step, nb::call_guard<nb::gil_scoped_release>())
        .def("set_threshold", &buan::BuanEngine::set_threshold)
        .def("set_price_spike_limit", &buan::BuanEngine::set_price_spike_limit)
        .def("set_max_vol_limit", &buan::BuanEngine::set_max_vol_limit) // Removed stray semicolon here
        .def("step_multi", [](buan::BuanEngine& engine, int count) {
            int captured = 0;
            for(int i = 0; i < count; ++i) {
                if(engine.step() == buan::EngineStatus::SIGNAL_CAPTURED) captured++;
            }
            return captured;
        }, nb::call_guard<nb::gil_scoped_release>()); // Semicolon goes here at the end of the chain

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