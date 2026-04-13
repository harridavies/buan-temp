#!/bin/bash
INTERFACE=$1
QUEUE=$2
PORT=$3

if [ -z "$PORT" ]; then
  echo "Usage: sudo ./steer_traffic.sh [interface] [queue] [port]"
  exit 1
fi

echo "[BuanAlpha] Steering UDP Port $PORT to Queue $QUEUE on $INTERFACE..."

# Use ethtool to create an N-tuple filter
ethtool -N $INTERFACE rx-flow-hash udp4 sdfn
ethtool -N $INTERFACE flow-type udp4 dst-port $PORT action $QUEUE

echo "OK: Hardware filter applied."