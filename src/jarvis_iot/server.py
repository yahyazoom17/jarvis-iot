import json
import logging
from pathlib import Path
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, HTTPException
from fastapi.responses import FileResponse
from jarvis_iot.utils import save_mic_audio_to_file, invoke_jarvis

app = FastAPI(title="JARVIS Websockets Server")

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("jarvis")

TEMP_DIR = Path("temp")
TEMP_DIR.mkdir(parents=True, exist_ok=True)

SAMPLE_RATE = 16000
CHANNELS = 1
SAMPLE_WIDTH = 2  # 16-bit PCM

clients: list[WebSocket] = []

@app.get("/")
async def root():
    return {
        "status": "running",
        "websocket": "/ws",
        "audio_format": "pcm_s16le",
        "sample_rate": SAMPLE_RATE,
        "duration_limit": None,
    }

@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket):
    await ws.accept()
    print(f"[WS] Client connected")
    clients.append(ws)
    print("[WS] Clients connected:", len(clients))
    audio_buffer = bytearray()
    recording = False

    try:
        while True:
            message = await ws.receive()
            # ==================================================
            # JSON / TEXT
            # ==================================================
            if message.get("text") is not None:
                data = message["text"]
                try:
                    msg = json.loads(data)
                except json.JSONDecodeError:
                    print("[WS] Invalid JSON:", data)
                    continue
                message_type = msg.get("type")

                # ==============================================
                # AUDIO START
                # ==============================================

                if message_type == "audio_start":
                    print("[WS] >>> AUDIO START")
                    print("Sample rate:", msg.get("sample_rate"))
                    print("Channels:", msg.get("channels"))
                    print("Bits:", msg.get("bits"))
                    print("Encoding:",msg.get("encoding"))
                    audio_buffer.clear()
                    recording = True

                # ==============================================
                # AUDIO END
                # ==============================================

                elif message_type == "audio_end":
                    print("[WS] >>> AUDIO END")
                    recording = False
                    print("[WS] Total audio:", len(audio_buffer), "bytes")
                    # Save PCM for testing
                    save_mic_audio_to_file(audio_buffer, "received_audio.wav")
                    print("[WS] Audio saved")
    except WebSocketDisconnect:
        print("[WS] Client disconnected")
        clients.remove(ws)
        print("[WS] Clients connected:", len(clients))

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
