import torch
import buan_alpha as ba

def sync_tick_to_torch(tick):
    """
    Ingests a BuanMarketTick directly into PyTorch with ZERO COPY.
    """
    # Get the raw memory view from our new C++ binding
    view = ba.get_tick_view(tick)
    
    # Convert to torch tensor (still zero copy)
    tensor = torch.from_blob(
        view.__array_interface__['data'][0], 
        shape=(1, 64), # 64 bytes total
        dtype=torch.uint8
    )
    
    return tensor

# Example usage in the prediction loop
# engine = ba.Engine(...)
# while True:
#     if engine.step() == ba.EngineStatus.SIGNAL_CAPTURED:
#         tick = ring_buffer.pop()
#         signal_tensor = sync_tick_to_torch(tick)
#         prediction = model(signal_tensor) # Inference starts in <100ns