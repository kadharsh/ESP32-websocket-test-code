import asyncio
import websockets
import json

async def handle_connection(websocket, path):
    try:
        print(f"[DEBUG] Client connected from {websocket.remote_address}")

        while True:
            try:
                # Wait for a message from the client
                message = await websocket.recv()
                
                # Handle binary data
                if isinstance(message, bytes):
                    print(f"Received binary packet of size: {len(message)} bytes")

                # Handle JSON messages
                else:
                    try:
                        packet = json.loads(message)
                        print(f"Received JSON packet of size: {len(message)} bytes")
                    except json.JSONDecodeError:
                        print(f"Received invalid JSON message of size: {len(message)} bytes")

            except websockets.exceptions.ConnectionClosed:
                print("[DEBUG] Client disconnected")
                break

    except Exception as e:
        print(f"[ERROR] Error handling connection: {str(e)}")

async def main():
    try:
        server = await websockets.serve(
            handle_connection,
            "0.0.0.0",
            8765
        )
        print("[DEBUG] WebSocket server started on ws://0.0.0.0:8765")
        await server.wait_closed()
    except Exception as e:
        print(f"[ERROR] Failed to start server: {str(e)}")

if __name__ == "__main__":
    asyncio.run(main())
