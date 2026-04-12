import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os

CPU_GHZ = 3.0 # Adjusted for M-series/OrbStack typical freq

def analyze():
    filename = "latency_report.csv"
    if not os.path.exists(filename):
        print(f"Error: {filename} not found. Did you wait for hela_audit to finish?")
        return

    df = pd.read_csv(filename)
    df['latency_ns'] = df['latency_cycles'] / CPU_GHZ
    
    p50 = np.percentile(df['latency_ns'], 50)
    p95 = np.percentile(df['latency_ns'], 95)
    
    print(f"\nBuanAlpha Performance Report (M-Series Dev Mode):")
    print(f"--------------------------------------------------")
    print(f"Median (P50) Latency: {p50:.2f} ns")
    print(f"Tail (P95) Latency:   {p95:.2f} ns")
    
    plt.figure(figsize=(10, 6))
    plt.hist(df['latency_ns'], bins=50, color='#1f77b4', alpha=0.8, label='BuanAlpha Path')
    plt.axvline(50000, color='red', linestyle='--', label='Standard Linux Stack (50k ns)')
    
    plt.title("Atomic Path Latency: BuanAlpha vs Standard Stack")
    plt.xlabel("Latency (nanoseconds)")
    plt.ylabel("Sample Count")
    plt.legend()
    plt.grid(axis='y', alpha=0.3)
    
    plt.savefig("atomic_gap.png")
    print(f"--------------------------------------------------")
    print("Success: Analysis saved to 'atomic_gap.png'")

if __name__ == "__main__":
    analyze()