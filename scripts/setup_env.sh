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

# 4. CPU C-States (advisory only)
# Suggestion: set processor.max_cstate=0 in GRUB for production

echo "[BuanAlpha] System Status:"
grep HugePages /proc/meminfo | head -n 3

THP_STATUS=$(cat /sys/kernel/mm/transparent_hugepage/enabled | grep -o '\[never\]')
echo "THP Status: ${THP_STATUS}"

echo ""
echo "[BuanAlpha] Environment provisioned. System is ready for the Atomic Path."
echo "Note: Run 'set_rt_priority' within the application to finalize the moat."