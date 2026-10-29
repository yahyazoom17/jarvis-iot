# Dockerfile for JARVIS-IOT - Hugging Face Spaces deployment
# https://huggingface.co/spaces
#
# IMPORTANT: API keys (ELEVENLABS_API_KEY, OPENROUTER_API_KEY) must be
# set as environment variables when running the container, not baked into the image.

# Use Python 3.12 slim image (matches .python-version)
FROM python:3.12-slim

# Install system dependencies for audio packages (PortAudio, ffmpeg)
RUN apt-get update && apt-get install -y --no-install-recommends \
    libportaudio2 \
    ffmpeg \
    && rm -rf /var/lib/apt/lists/*

# Install uv (fast Python package manager by Astral)
RUN pip install uv

# Set work directory
WORKDIR /app

# Copy project configuration files
COPY pyproject.toml uv.lock ./

# Install dependencies using uv from locked requirements
# --frozen ensures reproducible builds from uv.lock
# --system installs packages globally (needed for container runtime)
RUN uv sync --frozen --system

# Copy the jarvis_iot source code
COPY src/jarvis_iot ./jarvis_iot

# Expose the port HF Spaces expects (7860)
# Note: uvicorn server:app --port 7860 works because uvicorn imports the `app`
# FastAPI object from the module WITHOUT executing the `if __name__ == "__main__"`
# block, which hardcodes port 8000.
EXPOSE 7860

# Default port can be overridden at runtime via PORT env var (HF Spaces sets this)
# Using shell form for CMD so $PORT gets expanded
CMD uvicorn jarvis_iot.server:app --host 0.0.0.0 --port $PORT