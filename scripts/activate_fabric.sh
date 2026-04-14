#!/bin/bash
# BuanAlpha: Master "One-Click" Production Provisioner
# Targets: AWS Bare Metal (m6i.metal / m7i.metal) & High-Performance Data Centers

set -e

# Colors for professional output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${BLUE}[BuanAlpha] Initializing Production Provisioning...${NC}"

# 1. Dependency Installation (Task 10.1)
echo -e "${BLUE}[BuanAlpha] Installing Kernel-Bypass & Build Dependencies...${NC}"
sudo apt-get update
sudo apt-get install -y \
    clang llvm libbpf-dev libxdp-dev libnuma-dev libpcap-dev \
    cmake pkg-config build-essential ethtool cpupower \
    python3-pip nanobind

# 2. Hardware Hardening (Consolidated Task 10.1.1)
echo -e "${BLUE}[BuanAlpha] Applying Hardware Shielding...${NC}"
chmod +x scripts/setup_env.sh scripts/apply_grub_isolation.sh scripts/shield_interrupts.sh

# Provision HugePages and disable power-saving jitter
sudo ./scripts/setup_env.sh

# Apply GRUB isolation for Cores 1-3 (Requires Reboot)
# Note: We check if isolation is already applied to avoid redundant reboots
if ! grep -q "isolcpus" /etc/default/grub; then
    echo -e "${RED}[BuanAlpha] WARNING: Isolation parameters not found in GRUB.${NC}"
    sudo ./scripts/apply_grub_isolation.sh
    echo -e "${RED}[BuanAlpha] REBOOT REQUIRED to activate CPU isolation. Please reboot and run this script again.${NC}"
    exit 0
fi

# 3. Build Optimized Binary (Task 10.2.1)
echo -e "${BLUE}[BuanAlpha] Building Optimized "Iron" Fabric...${NC}"
chmod +x scripts/build.sh
./scripts/build.sh

# 4. Iron Health Check (Task 10.3)
echo -e "${BLUE}[BuanAlpha] Running 'Iron' Performance Certification...${NC}"
INTERFACE=$(ip route get 8.8.8.8 | awk '{print $5; exit}')
echo -e "${BLUE}[BuanAlpha] Auto-detected primary interface: $INTERFACE${NC}"

# Shield the interrupts on the detected interface
sudo ./scripts/shield_interrupts.sh "$INTERFACE"

# Task 10.2.2: PTP Readiness Check
PTP_DEV="/dev/ptp0"
if [ -e "$PTP_DEV" ]; then
    echo -e "${GREEN}[BuanAlpha] OK: PTP Hardware Clock found at $PTP_DEV.${NC}"
else
    echo -e "${RED}[BuanAlpha] WARNING: No PTP device found. High-precision audit may drift.${NC}"
fi

# Run a 30-second Market Storm to verify the L3-Moat
echo -e "${BLUE}[BuanAlpha] Launching verification storm...${NC}"
sudo ./build/buan_engine "$INTERFACE" 0 1 --stimulate &
STIM_PID=$!
sleep 10 # Allow the storm to saturate the arena
sudo kill -SIGINT $STIM_PID

echo -e "${GREEN}---------------------------------------------------${NC}"
echo -e "${GREEN}BuanAlpha: Provisioning Successful${NC}"
echo -e "${GREEN}Status: HARDWARE_CERTIFIED${NC}"
echo -e "${GREEN}---------------------------------------------------${NC}"