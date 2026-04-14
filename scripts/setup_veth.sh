#!/bin/bash
# Setup virtual networking for BuanAlpha Market Storm

if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root (sudo)" 
   exit 1
fi

echo "[BuanAlpha] Creating veth pair (buan_tx <-> buan_rx)..."

# Create the pair
ip link add buan_tx type veth peer name buan_rx

# Bring them up
ip link set buan_tx up
ip link set buan_rx up

# Assign dummy IPs for UDP routing
ip addr add 192.168.100.1/24 dev buan_tx
ip addr add 192.168.100.2/24 dev buan_rx

# Disable offloads to ensure raw processing
ethtool -K buan_rx gro off lro off

echo "OK: veth pair ready. Run engine on buan_rx and stimulator on 192.168.100.2"