import asyncio
import websockets
import json
import numpy as np
import buan_alpha as ba
import time

# Initialize the bridge to the SHM Arena
# Note: In a real "Stealth" setup, you'd use a separate process that calls buan.Client.attach()
# Here we assume the engine is running or we attach to the /buan_market_arena SHM
class AlphaVisionServer:
    def __init__(self, host="0.0.0.0", port=8080):
        self.host = host
        self.port = port
        # Attach to the SHM Arena created by the engine
        # For simulation, we assume 'engine' is reachable or SHM is mapped
        print(f"[Alpha-Vision] Attaching to Market Arena...")

    async def stream_market_forest(self, websocket):
        print(f"[Alpha-Vision] Client Connected: {websocket.remote_address}")
        try:
            while True:
                # 1. Capture 10Hz Snapshot
                # In production, we pull from the SHM pointer directly
                # For this demo, we generate a mock or use the engine if available
                z_scores = np.random.normal(0, 1, 2048).tolist() 
                
                # 2. Package as a Heatmap Update (32x64 Grid)
                payload = {
                    "timestamp": time.time(),
                    "z_scores": z_scores
                }
                
                await websocket.send(json.dumps(payload))
                await asyncio.sleep(0.1) # 10Hz
        except websockets.exceptions.ConnectionClosed:
            print("[Alpha-Vision] Client Disconnected.")

    async def start(self):
        print(f"[Alpha-Vision] Server starting on ws://{self.host}:{self.port}")
        async with websockets.serve(self.stream_market_forest, self.host, self.port):
            await asyncio.Future()  # run forever

if __name__ == "__main__":
    server = AlphaVisionServer()
    asyncio.run(server.start())