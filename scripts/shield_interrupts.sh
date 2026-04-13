#!/bin/bash
# BuanAlpha: Hardware Interrupt (IRQ) Shield

if [[ $EUID -ne 0 ]]; then
  echo "Error: Root privileges required."
  exit 1
fi

INTERFACE=${1:-eth0}
echo "[BuanAlpha] Shielding Cores from $INTERFACE Interrupts..."

# 1. Kill the OS auto-balancer which would otherwise undo our work
systemctl stop irqbalance 2>/dev/null
systemctl disable irqbalance 2>/dev/null

# 2. Force all general interrupts to Core 0
for irq_dir in /proc/irq/*; do
    if [ -d "$irq_dir" ]; then
        echo 1 > "$irq_dir/smp_affinity" 2>/dev/null
    fi
done

# 3. Specifically target the NIC's IRQ channels (for Multi-Queue support)
IRQ_IDS=$(grep "$INTERFACE" /proc/interrupts | awk '{print $1}' | sed 's/://')

for irq in $IRQ_IDS; do
    echo "[BuanAlpha] Pinning IRQ $irq to Core 0"
    echo 1 > "/proc/irq/$irq/smp_affinity"
done

echo "OK: Hardware interrupt shield active on Core 0."