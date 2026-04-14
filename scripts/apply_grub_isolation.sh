#!/bin/bash
# BuanAlpha: Kernel Boot Parameter Provisioner (Production Shield)

if [[ $EUID -ne 0 ]]; then
  echo "Error: Root privileges required."
  exit 1
fi

GRUB_FILE="/etc/default/grub"
BACKUP_FILE="/etc/default/grub.buan.bak"

echo "[BuanAlpha] Hardening Hardware Shield (Cores 1-3)..."

# Parameters explained:
# isolcpus: Keeps the kernel scheduler off these cores.
# nohz_full: Removes the 1ms timer tick interrupt (Tickless Mode).
# rcu_nocbs: Offloads RCU callbacks to Core 0.
# cstate: Prevents CPU from entering power-saving sleep modes (0 latency wake).
ISOL_PARAMS="isolcpus=1-3 nohz_full=1-3 rcu_nocbs=1-3 processor.max_cstate=0 intel_idle.max_cstate=0"

if [ ! -f "$BACKUP_FILE" ]; then
    cp "$GRUB_FILE" "$BACKUP_FILE"
    echo "OK: Created backup at $BACKUP_FILE"
fi

# Clean existing Buan parameters to avoid duplicates
sed -i 's/isolcpus=[^ ]* //g' "$GRUB_FILE"
sed -i 's/nohz_full=[^ ]* //g' "$GRUB_FILE"

# Inject the new high-performance parameters
sed -i 's/GRUB_CMDLINE_LINUX_DEFAULT="/GRUB_CMDLINE_LINUX_DEFAULT="'"$ISOL_PARAMS"' /' "$GRUB_FILE"

if command -v update-grub > /dev/null; then
    update-grub
elif command -v grub2-mkconfig > /dev/null; then
    grub2-mkconfig -o /boot/grub2/grub.cfg
fi

echo "[BuanAlpha] SUCCESS: Shield configured. REBOOT REQUIRED to activate the isolation."