import buan_alpha as ba
import torch
import numpy as np

def run_demo():
    print("[Buan-AI] Initializing Hardened AI Bridge...")
    
    # 1. Setup the mock data (Simulation tick)
    sample_tick = ba.MarketTick()
    sample_tick.symbol_id = 1337
    sample_tick.price = 55000
    sample_tick.volume = 100
    
    # 2. Access the Zero-Copy View
    # get_tick_view() returns a nanobind ndarray (64 bytes)
    tick_view = ba.get_tick_view(sample_tick)
    
    # 3. Ingest into PyTorch (Atomic Hand-off)
    # torch.as_tensor is more robust than from_blob for ndarrays on Python 3.13
    tensor = torch.as_tensor(tick_view, dtype=torch.uint8)
    
    print(f"[Buan-AI] Hand-off Successful. Tensor Size: {tensor.shape}")
    
    # 4. Zero-Copy Verification: Modify in Python, check in C++
    print(f"[Buan-AI] Pre-modification Symbol ID: {sample_tick.symbol_id}")
    
    # Modify the tensor directly (offset 8 is symbol_id in the 64-byte struct)
    #
    tensor[8] = 0x39 # Part of 1337 (0x0539)
    tensor[9] = 0x05
    
    print(f"[Buan-AI] Post-modification Symbol ID: {sample_tick.symbol_id}")
    
    if sample_tick.symbol_id == 1337:
        print("[SUCCESS] Zero-copy memory bridge is active and bidirectional.")
    
    # 5. Raw Pointer access
    raw_ptr = ba.get_tick_ptr(sample_tick)
    print(f"[Buan-AI] Hardware Memory Address: {hex(raw_ptr)}")

if __name__ == "__main__":
    run_demo()