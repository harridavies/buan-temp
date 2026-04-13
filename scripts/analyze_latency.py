import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os
import sys

# Hardware configuration
CPU_GHZ = 3.0 
LINUX_BASELINE_NS = 50000.0 # Standard kernel-bypass/poll latency overhead

def calculate_latency_tax(p50_ns, daily_volume_usd, slippage_bps_per_100ns):
    """
    Calculates the 'Bribe' - the financial cost of suboptimal latency.
    Formula: (Volume * (Latency Delta / 100ns)) * (Slippage BPS / 10000)
    """
    latency_delta = LINUX_BASELINE_NS - p50_ns
    if latency_delta <= 0:
        return 0.0
    
    # Calculate impact points (how many 100ns chunks are saved)
    impact_units = latency_delta / 100.0
    # Calculate dollar loss per day
    loss_per_day = (daily_volume_usd * impact_units) * (slippage_bps_per_100ns / 10000.0)
    return loss_per_day

def analyze(volume=1_000_000_000, slippage=0.01):
    filename = "latency_report.csv"
    if not os.path.exists(filename):
        print(f"!! [Error] {filename} not found. Run 'hela_audit' first.")
        return

    # Skip the CAR-2026 compliance headers (first 3 lines) if present
    try:
        df = pd.read_csv(filename, skiprows=3)
    except Exception:
        df = pd.read_csv(filename) # Fallback if headers are missing

    # Handle different CSV formats (cycles vs ns)
    if 'latency_cycles' in df.columns:
        df['latency_ns'] = df['latency_cycles'] / CPU_GHZ
    elif 'delta_ns' in df.columns:
        df['latency_ns'] = df['delta_ns']
    
    p50 = np.percentile(df['latency_ns'], 50)
    p95 = np.percentile(df['latency_ns'], 95)
    p99 = np.percentile(df['latency_ns'], 99)
    
    daily_tax = calculate_latency_tax(p50, volume, slippage)
    
    print(f"\n[BuanAlpha] Performance Report: THE ATOMIC GAP")
    print(f"--------------------------------------------------")
    print(f"Median (P50) Latency: {p50:7.2f} ns")
    print(f"Tail   (P95) Latency: {p95:7.2f} ns")
    print(f"Tail   (P99) Latency: {p99:7.2f} ns")
    print(f"--------------------------------------------------")
    print(f"ESTIMATED DAILY LATENCY TAX (Linux vs BuanAlpha)")
    print(f"Assumed Daily Volume: ${volume:,.0f}")
    print(f"Projected Loss/Day:   ${daily_tax:,.2f}")
    print(f"--------------------------------------------------")

    # Generate Professional Chart
    plt.style.use('dark_background')
    plt.figure(figsize=(12, 7))
    
    plt.hist(df['latency_ns'], bins=100, color='#00ff41', alpha=0.6, label='BuanAlpha Atomic Path')
    plt.axvline(LINUX_BASELINE_NS, color='#ff003c', linestyle='--', linewidth=2, label='Standard Linux Stack')
    plt.axvline(p50, color='#00ff41', linestyle='-', alpha=0.5, label=f'BuanAlpha P50 ({p50:.1f}ns)')
    
    plt.title("BuanAlpha vs Standard Stack: The $ Daily Gap")
    plt.xlabel("Latency (nanoseconds)")
    plt.ylabel("Packet Count")
    plt.yscale('log') # Log scale helps visualize tail latency
    plt.legend()
    plt.grid(alpha=0.2)
    
    plt.savefig("atomic_gap.png")
    print("Report saved to 'atomic_gap.png'")

if __name__ == "__main__":
    # Allow command line overrides for the pitch
    vol = float(sys.argv[1]) if len(sys.argv) > 1 else 1_000_000_000
    slip = float(sys.argv[2]) if len(sys.argv) > 2 else 0.01
    analyze(vol, slip)