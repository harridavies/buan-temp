#!/bin/bash
# BuanAlpha: Kernel Boot Parameter Provisioner

if [[ $EUID -ne 0 ]]; then
  echo "Error: Root privileges required."
  exit 1
fi

echo "[BuanAlpha] Modifying GRUB for Core Isolation (Cores 1-3)..."

# Parameters:
# isolcpus: Prevents the scheduler from using these cores.
# nohz_full: Enables tickless mode (removes the 1ms timer interrupt).
# rcu_nocbs: Moves RCU callback processing off these cores.
ISOL_PARAMS="isolcpus=1-3 nohz_full=1-3 rcu_nocbs=1-3 processor.max_cstate=0 intel_idle.max_cstate=0"

# Backup and Update GRUB
sed -i 's/GRUB_CMDLINE_LINUX_DEFAULT="/GRUB_CMDLINE_LINUX_DEFAULT="'"$ISOL_PARAMS"' /' /etc/default/grub

update-grub

echo "[BuanAlpha] SUCCESS: Kernel parameters applied. System reboot required to activate the shield."