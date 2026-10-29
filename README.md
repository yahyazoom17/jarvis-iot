# Jarvis IoT

A voice-first personal AI assistant ("Jarvis") that chains **speech-to-text → tool-calling agent → LLM response → text-to-speech**, exposed as a small FastAPI HTTP service.

The project is an early-stage prototype: the end-to-end loop works, but the transport layer is synchronous and file-based rather than a true streaming voice pipeline.

---

## How it works

```
user query (text)
      │
      ▼
POST /jarvis/ask  ──►  invoke_jarvis()
                            │
                            ├─► transcribe_audio()               Needle Whistle (local STT)
                            │
                            ├─► jarvis_agent.run()               Needle (local on-device SLM)
                            │      └─► tools: get_weather, evaluate_maths, get_time
                            │
                            ├─► get_jarvis_response()            litellm → Ollama llama3.2:1b
                            │      └─► turns tool results into one short spoken sentence
                            │
                            └─► generate_speech()                ElevenLabs TTS → response.wav
```

1. **Tool selection (`jarvis.py`)** — A local [Needle](https://github.com/cactus-compute/needle) agent (`cactus-needle`) receives the user's command and picks/runs one of the registered `@needle.tool` functions. This runs on-device; no network round trip.
2. **Response generation (`llm.py`)** — The tool output is handed to a chat model via `litellm`. The system prompt (`config.py`) constrains the model to be warm, concise, factual, and to answer in **one short sentence** suited for speech.
3. **Speech synthesis (`speech_synthesis.py`)** — The reply text is converted to audio with ElevenLabs (`eleven_multilingual_v2`, `wav_22050`) and written to `response.wav`.
4. **Transcription (`jarvis.py`)** — `needle.transcribe()` (Needle "Whistle") provides local speech-to-text, currently invoked on `response.wav`.

---

## Project layout

```
.
├── pyproject.toml                     # uv-managed project metadata + deps
├── env-example                        # template for required secrets
├── uv.lock
├── Dockerfile                         # multi-stage image (deps + pre-warmed Needle)
├── docker-compose.yml                 # optional: app + local Ollama
├── .dockerignore
├── scripts/
│   └── prewarm_needle.py              # downloads the Needle engine at build time
└── src/jarvis_iot/
    ├── __init__.py                    # `main()` placeholder entry point
    ├── server.py                      # FastAPI app + routes
    ├── utils.py                       # invoke_jarvis(): the full pipeline
    ├── jarvis.py                      # Needle tools + agent instance + STT wrapper
    ├── llm.py                         # litellm completion call
    ├── speech_synthesis.py            # ElevenLabs text-to-speech
    ├── config.py                      # system + user prompt templates
    ├── response.wav                   # TTS output / STT input file
    ├── schemas/
    │   └── QuerySchema.py             # UserQuery pydantic model
    └── test/
        └── harvard.wav                # sample audio for manual transcription tests
```

---

## Requirements

- Python **3.12+**
- [uv](https://docs.astral.sh/uv/) (recommended; see `uv.lock`)
- [Ollama](https://ollama.com/) running locally with the `llama3.2:1b` model pulled
- An **ElevenLabs** API key

## Setup

```bash
# install dependencies
uv sync

# configure secrets
cp env-example .env

# pull the local model used by llm.py
ollama pull llama3.2:1b
```

`.env` keys:

| Variable             | Required | Purpose                                        |
| -------------------- | -------- | ---------------------------------------------- |
| `ELEVENLABS_API_KEY` | yes      | text-to-speech                                 |
| `OPENROUTER_API_KEY` | no       | only for the commented-out OpenRouter backend  |
| `HF_TOKEN`           | no       | optional Hugging Face access                   |

## Run

```bash
# HTTP API on 0.0.0.0:8000
uv run python -m jarvis_iot.server

# or with reload during development
uv run uvicorn jarvis_iot.server:app --reload
```

---

## Run with Docker

The image is multi-stage: a `uv` builder installs dependencies from the
lockfile and pre-downloads the Needle engine, then a slim `python:3.12-slim-bookworm`
runtime copies in the venv, the source, and the ~54 MB engine cache. Baking the
cache in means the container starts instantly and never needs Hugging Face (or a
network) at runtime — at the cost of a slightly larger image.

### Quick start (bundled Ollama)

```bash
cp env-example .env                       # set ELEVENLABS_API_KEY

docker compose up --build
docker compose exec ollama ollama pull llama3.2:1b

curl http://localhost:8000/health
```

`docker-compose.yml` wires the app to the `ollama` service via `OLLAMA_API_BASE=http://ollama:11434`.

### Image only (your own Ollama)

```bash
docker build -t jarvis-iot .

docker run --rm -p 8000:8000 \
  -e ELEVENLABS_API_KEY="$ELEVENLABS_API_KEY" \
  -e OLLAMA_API_BASE=http://host.docker.internal:11434 \
  jarvis-iot
```

On Linux, `host.docker.internal` is not available by default — add
`--add-host=host.docker.internal:host-gateway`, or use the host's bridge IP
(usually `172.17.0.1`).

> `llm.py` reads `OLLAMA_API_BASE` and `OLLAMA_MODEL`; they default to
> `http://localhost:11434` and `ollama/llama3.2:1b`, so nothing changes for
> local, non-Docker runs.

### Notes

- **Runtime user:** the app runs as an unprivileged `app` user (uid 1000). `/app`
  is writable because `utils.py` writes `response.wav` relative to the working
  directory — mount a volume there if you want the audio to survive restarts.
- **Needle cache:** lives at `/home/app/.cache/cactus-needle`. It is baked in at
  build time; add a named volume over it if you plan to bump Needle versions
  without rebuilding the whole image.
- **Optional HF token:** the build downloads from public Hugging Face repos, so
  no token is required. Pass one only to raise the rate limit:

  ```bash
  docker build --secret id=hf_token,env=HF_TOKEN -t jarvis-iot .
  ```
- **Microphone:** `libportaudio2` and `sndfile1` are installed so Needle's
  `[mic]` extra works, but the container is not wired for live audio capture —
  you would need `--device /dev/snd` and a PulseAudio/ALSA socket.

---

## API

| Method | Path           | Body                     | Description                                  |
| ------ | -------------- | ------------------------ | -------------------------------------------- |
| `GET`  | `/`            | —                        | Welcome message                              |
| `GET`  | `/health`      | —                        | Liveness check                               |
| `POST` | `/jarvis`      | —                        | Readiness stub for the agent                 |
| `POST` | `/jarvis/ask`  | `{ "query": "..." }`     | Runs the full assistant pipeline             |

Example:

```bash
curl -X POST http://localhost:8000/jarvis/ask \
  -H "Content-Type: application/json" \
  -d '{"query": "What is the weather in Cairo?"}'
```

On success the assistant's text reply is returned in `message`, and the spoken
version is written to `response.wav`.

---

## Built-in tools

| Tool               | Description                                        |
| ------------------ | -------------------------------------------------- |
| `get_weather`      | Stubbed weather lookup (always 27 °C, clear sky)    |
| `evaluate_maths`   | Evaluates an arithmetic expression via `eval()`     |
| `get_time`         | Current local time and date                         |

---

## Tech stack

| Layer            | Technology                    |
| ---------------- | ----------------------------- |
| HTTP server      | FastAPI + Uvicorn             |
| Agent / tools    | Needle (`cactus-needle`)      |
| Speech-to-text   | Needle Whistle (local)        |
| LLM routing      | LiteLLM → Ollama `llama3.2:1b`|
| Text-to-speech   | ElevenLabs                    |
| Validation       | Pydantic                      |
| Packaging        | uv / uv_build                 |

---

## Known limitations & next steps

- **Not a real voice pipeline yet.** `invoke_jarvis()` accepts text but transcribes a hard-coded `response.wav`, and TTS writes back to the same `response.wav`. Audio upload and per-request audio paths are the obvious next step.
- **No audio streaming.** Everything is synchronous request/response; no chunked STT or streamed TTS.
- **Global chat history.** `llm.py` appends to a module-level `messages` list, so conversation state leaks across requests and grows unboundedly. Reset per request or key it by session.
- **`eval()` on user input.** `evaluate_maths` evaluates arbitrary expressions; replace with a safe math parser.
- **Hard-coded models.** The Ollama endpoint is now configurable via `OLLAMA_API_BASE` / `OLLAMA_MODEL`, but the ElevenLabs voice ID (`EXAVITQu4vr4xnSDxMaL`) and model (`eleven_multilingual_v2`) are still literals — move them into configuration.
- **Dependency gaps.** `langgraph` is declared but unused. The `jarvis-iot` console script only prints a placeholder; it does not start the server.
- **No automated tests.** `src/jarvis_iot/test/` holds only sample audio (`harvard.wav`).
- **Packaging polish.** `pyproject.toml` still has the placeholder description, and the generated `src/jarvis_iot/response.wav` is tracked in git — add it to `.gitignore` (`response.wav`, `*.venv`).
- **Image is unverified.** `Dockerfile`, `.dockerignore`, and `docker-compose.yml` have not been built against a running Docker daemon; the Needle pre-download (`scripts/prewarm_needle.py`) was verified locally against both warm and cold caches.
