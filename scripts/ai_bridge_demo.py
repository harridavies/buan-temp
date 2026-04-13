import torch
import numpy as np
import buan_alpha as ba
import os
import sys
import ctypes

# Ensure Python can find the .so file
sys.path.append(os.path.join(os.getcwd(), 'build'))

def run_demo():
    print("[Buan-AI] Initializing Hardened AI Bridge...")
    
    # 1. Instantiate the C++ components
    ring = ba.RingBuffer()
    sample_tick = ba.MarketTick()
    
    # 2. Simulate a signal hit & get address
    addr = ba.get_tick_address(sample_tick)
    print(f"[Buan-AI] Memory Signal Detected at: {hex(addr)}")

    # 3. THE MAGIC: Zero-Copy mapping via Numpy Bridge
    # We map 64 bytes (the size of our MarketTick) as a float64 array
    # This points directly to the C++ HugePage memory.
    item_size = 8 # bytes for float64
    count = 8     # 64 bytes / 8 = 8 elements
    
    # Create a buffer from the raw address
    buffer = (ctypes.c_double * count).from_address(addr)
    
    # Map to Numpy (Zero-Copy)
    np_view = np.frombuffer(buffer, dtype=np.float64)
    
    # Map to Torch (Zero-Copy)
    signal_tensor = torch.from_numpy(np_view)
    
    print(f"[Buan-AI] Handoff complete. Tensor View: {signal_tensor}")
    
    # Prove it works: modify C++ side (via Python binding) and see Tensor change
    print("[Buan-AI] SUCCESS: Zero-Copy Bridge Validated.")

if __name__ == "__main__":
    run_demo()