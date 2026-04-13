#!/bin/bash
# BuanAlpha: Hardware Interrupt (IRQ) Shield

if [[ $EUID -ne 0 ]]; then
  echo "Error: Root privileges required."
  exit 1
fi

echo "[BuanAlpha] Shielding Cores 1-3 from Hardware Interrupts..."

# 1. Kill the auto-balancer
systemctl stop irqbalance
systemctl disable irqbalance

# 2. Mask all IRQs to Core 0 (mask '1' is bit 0)
# This forces every hardware event to be handled by the OS core only.
for irq_dir in /proc/irq/*; do
    if [ -d "$irq_dir" ]; then
        echo 1 > "$irq_dir/smp_affinity" 2>/dev/null
    fi
done

echo "OK: Interrupts moved to Core 0."