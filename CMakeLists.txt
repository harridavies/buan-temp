cmake_minimum_required(VERSION 3.20)
project(BuanAlphaFabric LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-O3 -march=native -Wall -Wextra -Wpedantic)

# Phase 6: Production Hardware Optimization
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    message(STATUS "BuanAlpha: Enabling Intel AVX-512 Production Extensions")
    add_compile_definitions(BUAN_AVX512_ENABLED)
    add_compile_options(-mavx512f -mavx512bw -mavx512dq -mavx512vl)
endif()

add_compile_options(-fno-plt -fno-semantic-interposition)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBBPF REQUIRED libbpf)
pkg_check_modules(LIBXDP REQUIRED libxdp)
find_library(LIBNUMA numa REQUIRED)
find_library(LIBPCAP pcap REQUIRED)

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
execute_process(
    COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    OUTPUT_VARIABLE nanobind_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT nanobind_DIR)
    execute_process(
        COMMAND "${Python_EXECUTABLE}" -c "import nanobind; print(nanobind.cmake_dir())"
        OUTPUT_VARIABLE nanobind_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()
find_package(nanobind CONFIG REQUIRED)

# eBPF Compilation Fix: Explicitly include architecture-specific headers
set(XDP_KERN_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/network/xdp_kern.c)
set(XDP_KERN_OBJ ${CMAKE_CURRENT_SOURCE_DIR}/src/network/xdp_kern.o)

# Detect multi-arch path for asm/types.h (e.g., /usr/include/aarch64-linux-gnu)
execute_process(COMMAND gcc -print-multiarch OUTPUT_VARIABLE ARCH_TRIPLET OUTPUT_STRIP_TRAILING_WHITESPACE)

add_custom_command(
    OUTPUT ${XDP_KERN_OBJ}
    COMMAND clang -O2 -target bpf -I/usr/include -I/usr/include/${ARCH_TRIPLET} -c ${XDP_KERN_SRC} -o ${XDP_KERN_OBJ}
    DEPENDS ${XDP_KERN_SRC}
    COMMENT "Compiling BuanXDPGatekeeper eBPF program for ${ARCH_TRIPLET}..."
)
add_custom_target(buan_gatekeeper ALL DEPENDS ${XDP_KERN_OBJ})

add_library(buan_core SHARED
    src/core/arena_allocator.cpp
    src/math/feature_factory.cpp
    src/network/protocol_parser.cpp
    src/network/binance_parser.cpp
    src/monads/datapath_composition.cpp
    src/network/xdp_portal.cpp
    src/network/pcap_portal.cpp
)

target_link_libraries(buan_core 
    PUBLIC ${LIBBPF_LIBRARIES} 
    PUBLIC ${LIBXDP_LIBRARIES} 
    PUBLIC ${LIBNUMA}
    PUBLIC ${LIBPCAP}
    PUBLIC pthread
    PUBLIC rt 
)

add_executable(buan_engine src/main.cpp) 
target_link_libraries(buan_engine PRIVATE buan_core)

add_executable(hela_audit src/benchmarks/hela_audit_suite.cpp)
target_link_libraries(hela_audit PRIVATE buan_core)

nanobind_add_module(buan_alpha src/api/python_bindings.cpp)
target_link_libraries(buan_alpha PRIVATE buan_core)

message(STATUS "BuanAlpha Fabric: Build System Initialized for ${ARCH_TRIPLET}")