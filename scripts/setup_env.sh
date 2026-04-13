#!/bin/bash

# BuanAlpha: Atomic-Latency Environment Provisioner
# Optimized for 2026 Tier-2 Quant Infrastructure

# Require root privileges
if [[ $EUID -ne 0 ]]; then
  echo "Error: BuanAlpha requires root privileges for kernel-bypass configuration."
  exit 1
fi

echo "[BuanAlpha] Configuring Kernel for Atomic Ingest..."

# 1. HugePage Reservation (Deterministic Memory)
# Reserve 1024 x 2MB pages (2GB)
sysctl -w vm.nr_hugepages=1024

# 2. Network Stack Optimization (Lowering Jitter)
sysctl -w net.core.netdev_max_backlog=20000
sysctl -w net.core.rmem_max=16777216
sysctl -w net.core.wmem_max=16777216

# 3. Transparent HugePages (THP)
# Disable THP to prevent jitter spikes
echo never > /sys/kernel/mm/transparent_hugepage/enabled
echo never > /sys/kernel/mm/transparent_hugepage/defrag

# 4. Power & Performance Locking (Phase 9)
# Lock the CPU in 'Performance' mode and disable frequency scaling jitter.
if command -v cpupower &> /dev/null; then
    cpupower -c all frequency-set -g performance
    echo "[BuanAlpha] CPU Governor set to PERFORMANCE."
else
    echo "[Warning] cpupower not found. Frequency jitter may occur."
fi

# Direct Hardware C-State Lock via sysfs
for i in /sys/devices/system/cpu/cpu*/cpuidle/state*/disable; do
    echo 1 > "$i"
done
echo "[BuanAlpha] All CPU Power-Saving C-States DISABLED."

echo "[BuanAlpha] System Status:"
grep HugePages /proc/meminfo | head -n 3

THP_STATUS=$(cat /sys/kernel/mm/transparent_hugepage/enabled | grep -o '\[never\]')
echo "THP Status: ${THP_STATUS}"

echo ""
echo "[BuanAlpha] Environment provisioned. System is ready for the Atomic Path."
echo "Note: Run 'set_rt_priority' within the application to finalize the moat."