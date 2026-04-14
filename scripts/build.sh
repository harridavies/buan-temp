#!/bin/bash

# BuanAlpha: Atomic-Latency Build Orchestrator
# Targets: C++23 Monadic Engine, eBPF Gatekeeper, and Python Nanobindings.

set -e  # Exit on any error

# Color formatting for terminal output
BLUE='\033[0;34m'
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${BLUE}[BuanAlpha] Starting Build Pipeline...${NC}"

# 1. Dependency Check
echo -e "${BLUE}[BuanAlpha] Checking system dependencies...${NC}"

# We check for the tools actually used in the build process
deps=("clang" "cmake" "pkg-config")
for dep in "${deps[@]}"; do
  if ! command -v "$dep" &> /dev/null; then
    echo -e "${RED}Error: $dep is not installed. Run: sudo apt install clang cmake pkg-config${NC}"
    exit 1
  fi
done

# Check for llvm-config (standard LLVM verification)
if ! command -v llvm-config &> /dev/null && ! command -v llvm-config-20 &> /dev/null; then
  echo -e "${RED}Error: llvm-config not found. Run: sudo apt install llvm${NC}"
  exit 1
fi

# Check for nanobind (Python side)
if ! python3 -c "import nanobind" &> /dev/null; then
  echo -e "${RED}Error: nanobind not found. Run: pip install nanobind${NC}"
  exit 1
fi

# 2. Directory Scaffolding
mkdir -p build

# 3. Environment Preparation
chmod +x scripts/setup_env.sh

# 4. Compilation
echo -e "${BLUE}[BuanAlpha] Generating build files via CMake...${NC}"
# Task 10.2.1: Verify Hardware Math Support
if grep -q "avx512" /proc/cpuinfo; then
    echo -e "${GREEN}[BuanAlpha] OK: AVX-512 detected. Production math enabled.${NC}"
else
    echo -e "${RED}[BuanAlpha] WARNING: AVX-512 not found. Falling back to scalar math.${NC}"
fi

cd build

# Use Clang for eBPF + C++23
export CC=clang
export CXX=clang++

# Run CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

echo -e "${BLUE}[BuanAlpha] Compiling Atomic Path components...${NC}"

# Use all available cores
make -j"$(nproc)"

# 5. Post-Build Verification
echo -e "${GREEN}---------------------------------------------------${NC}"
echo -e "${GREEN}BuanAlpha: Build Successful${NC}"
echo -e "${GREEN}---------------------------------------------------${NC}"

if [ -f "buan_engine" ]; then
  echo -e "Ready: Data Plane  -> build/buan_engine"
fi

if [ -f "hela_audit" ]; then
  echo -e "Ready: Audit Tool  -> build/hela_audit"
fi

# Find the Python .so file
PY_MOD=$(find . -name "buan_alpha_python*.so")
if [ -n "$PY_MOD" ]; then
  echo -e "Ready: Python Mod  -> build/$PY_MOD"
fi

echo -e "${BLUE}[BuanAlpha] To begin the hunt, run: sudo ../scripts/setup_env.sh${NC}"