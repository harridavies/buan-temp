#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/ndarray.h>
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
    // 1. Expose the Status Codes
    nb::enum_<buan::EngineStatus>(m, "EngineStatus")
        .value("IDLE", buan::EngineStatus::IDLE)
        .value("SIGNAL_CAPTURED", buan::EngineStatus::SIGNAL_CAPTURED)
        .value("FILTERED_BY_BREAKER", buan::EngineStatus::FILTERED_BY_BREAKER)
        .value("BUFFER_FULL", buan::EngineStatus::BUFFER_FULL)
        .value("PORTAL_EMPTY", buan::EngineStatus::PORTAL_EMPTY);

    // 2. Expose the Market Tick Structure
    nb::class_<buan::BuanMarketTick>(m, "MarketTick")
        .def_ro("ingress_tsc", &buan::BuanMarketTick::ingress_tsc)
        .def_ro("symbol_id", &buan::BuanMarketTick::symbol_id)
        .def_ro("price", &buan::BuanMarketTick::price)
        .def_ro("volume", &buan::BuanMarketTick::volume)
        .def_ro("signal_drift", &buan::BuanMarketTick::signal_drift);

    // 3. Expose the Engine Orchestrator
    nb::class_<buan::BuanEngine>(m, "Engine")
        .def("step", &buan::BuanEngine::step, nb::call_guard<nb::gil_scoped_release>())
        .def("set_threshold", &buan::BuanEngine::set_threshold);

    // 4. The "Secret Weapon": Zero-Copy Data Access
    // This allows Python to see the raw memory address for PyTorch integration.
    m.def("get_tick_address", [](const buan::BuanMarketTick& tick) {
        return reinterpret_cast<uintptr_t>(&tick);
    });
}