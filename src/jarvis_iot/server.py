import asyncio
import json
import logging
import wave
from pathlib import Path
from typing import Set
from jarvis_iot.utils import invoke_jarvis
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, HTTPException
from fastapi.responses import StreamingResponse
from starlette.websockets import WebSocketState

app = FastAPI(title="JARVIS WebSocket Server")

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("jarvis")

BASE_DIR = Path(__file__).resolve().parent
TEMP_DIR = BASE_DIR / "temp"
TEMP_DIR.mkdir(parents=True, exist_ok=True)

SAMPLE_RATE = 16_000
CHANNELS = 1
SAMPLE_WIDTH = 2  # 16-bit PCM = 2 bytes/sample

RESPONSE_FILE = Path("response.wav")

clients: Set[WebSocket] = set()

@app.get("/health")
async def health():
    return {
        "status": "running",
        "websocket": "/ws",
        "audio_format": "pcm_s16le",
        "sample_rate": SAMPLE_RATE,
        "channels": CHANNELS,
        "bits_per_sample": SAMPLE_WIDTH * 8,
    }


async def send_json_safe(ws: WebSocket, payload: dict) -> None:
    if ws.client_state == WebSocketState.CONNECTED:
        try:
            await ws.send_json(payload)
        except (RuntimeError, WebSocketDisconnect):
            pass


def save_pcm_as_wav(pcm_data: bytearray, output_path: Path) -> None:
    # Ensure byte count aligns precisely with 16-bit bounds before saving
    valid_length = len(pcm_data) - (len(pcm_data) % SAMPLE_WIDTH)
    with wave.open(str(output_path), "wb") as wav_file:
        wav_file.setnchannels(CHANNELS)
        wav_file.setsampwidth(SAMPLE_WIDTH)
        wav_file.setframerate(SAMPLE_RATE)
        wav_file.writeframes(pcm_data[:valid_length])

@app.get("/response")
def stream_response():
    if not RESPONSE_FILE.exists():
        raise HTTPException(
            status_code=404,
            detail="Response file not found"
        )
    # Get exact file size
    file_size = RESPONSE_FILE.stat().st_size

    def iter_file():
        with open(RESPONSE_FILE, "rb") as file:
            while True:
                chunk = file.read(16 * 1024)
                if not chunk:
                    break
                yield chunk


    return StreamingResponse(
        iter_file(),
        media_type="audio/wav",
        headers={
            # Tell ESP32 exactly how many bytes are coming
            "Content-Length": str(file_size),
            "Content-Disposition":f'inline; filename="{RESPONSE_FILE.name}"',
            "Cache-Control":"no-cache, no-store, must-revalidate",
            "Pragma":"no-cache",
        },
    )

@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket):
    await ws.accept()
    clients.add(ws)
    client_name = str(ws.client) if ws.client else "unknown client"
    logger.info("WebSocket connected: %s (clients=%d)", client_name, len(clients))

    audio_buffer = bytearray()
    recording = False

    try:
        while True:
            message = await ws.receive()

            if message.get("type") == "websocket.disconnect":
                break

            text_data = message.get("text")
            if text_data is not None:
                try:
                    payload = json.loads(text_data)
                except json.JSONDecodeError:
                    continue

                message_type = payload.get("type")

                if message_type == "audio_start":
                    audio_buffer.clear()
                    recording = True
                    logger.info("Audio recording started")
                    await send_json_safe(ws, {"status": "recording", "message": "Audio recording started"})

                elif message_type == "audio_end":
                    if not recording:
                        continue
                    
                    recording = False
                    output_path = TEMP_DIR / "received_audio.wav"
                    try:
                        # Writing to disk happens off the async loop to prevent blocking
                        await asyncio.to_thread(save_pcm_as_wav, audio_buffer, output_path)
                        
                        byte_count = len(audio_buffer)
                        duration_seconds = byte_count / (SAMPLE_RATE * CHANNELS * SAMPLE_WIDTH)
                        
                        logger.info("Saved %d bytes (%.2f sec) to %s", byte_count, duration_seconds, output_path)
                        
                        jarvis_response = await invoke_jarvis()  # Invoke the Jarvis agent to process the audio and generate a response
                        print(f"[FINAL RESPONSE] {jarvis_response}")
                        
                        await send_json_safe(ws, {
                            "status": "response_audio_saved",
                            "service":"jarvis_response",
                            "response_url":"/response",
                            "response_text": jarvis_response,
                        })
                    except Exception as exc:
                        logger.error("Failed to save audio: %s", exc)
                    finally:
                        audio_buffer.clear()

                elif message_type == "handshake":
                    await send_json_safe(ws, {"status": "ok", "service": "jarvis", "message": "ESP32 connected"})

            # Capture binary WebSocket data sent by the FreeRTOS Queue
            binary_data = message.get("bytes")
            if binary_data is not None and recording:
                audio_buffer.extend(binary_data)

    except WebSocketDisconnect:
        logger.info("WebSocket disconnected gracefully")
    finally:
        audio_buffer.clear()

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)