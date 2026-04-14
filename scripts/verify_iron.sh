#!/bin/bash
# BuanAlpha Hardware Verification Script

echo "[BuanAlpha] Checking Iron Integrity..."

# 1. Verify CPU (Looking for AVX-512 Support)
AVX512=$(grep -o "avx512" /proc/cpuinfo | head -n 1)
if [ -z "$AVX512" ]; then
    echo "!! [WARNING] AVX-512 not detected. Latency targets may fail."
else
    echo "OK: AVX-512 Instruction Set detected."
fi

# 2. Verify NIC (Looking for Mellanox ConnectX-6/7)
NIC_MODEL=$(lspci | grep -i "Mellanox")
if [ -z "$NIC_MODEL" ]; then
    echo "!! [CRITICAL] Mellanox NIC not found. XDP_ZERO_COPY will fail."
else
    echo "OK: Hardware NIC found: $NIC_MODEL"
fi

# 3. Check NUMA Topology
echo "[BuanAlpha] System Topology:"
lscpu | grep "NUMA node"