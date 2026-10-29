# syntax=docker/dockerfile:1

########################################################################
# Stage 1 -- dependencies + pre-warmed Needle engine
########################################################################
# bookworm (glibc) rather than alpine: needle loads a native libneedle.so that
# is published for glibc builds.
FROM ghcr.io/astral-sh/uv:python3.12-bookworm-slim AS builder

ENV UV_LINK_MODE=copy \
    UV_PYTHON_DOWNLOADS=never \
    UV_PROJECT_ENVIRONMENT=/app/.venv

WORKDIR /app

# Manifests only, so this layer is cached until the lockfile changes.
# README.md is required here: pyproject.toml declares readme = "README.md".
COPY pyproject.toml uv.lock README.md ./
RUN uv sync --frozen --no-dev --no-install-project

# Then the source and the prewarm helper, then the project itself.
COPY src ./src
COPY scripts ./scripts
RUN uv sync --frozen --no-dev

# needle fetches its native engine and model weights lazily on first use
# (~54 MB: libneedle.so + needle3.cact + whistle.cact) into $HOME/.cache.
# Baking them into the image makes the container start instantly and removes
# any runtime dependency on Hugging Face. The repos are public, so the secret
# is optional and only raises the unauthenticated rate limit.
#
#   docker build --secret id=hf_token,env=HF_TOKEN .
#
RUN --mount=type=secret,id=hf_token,required=false \
    HF_TOKEN="$(cat /run/secrets/hf_token 2>/dev/null || true)" \
    HOME=/app uv run --no-sync python scripts/prewarm_needle.py

########################################################################
# Stage 2 -- runtime
########################################################################
FROM python:3.12-slim-bookworm AS runtime

# The venv is copied from the builder stage, whose interpreter lives at the same
# path (/usr/local/bin/python3.12) as this image's. Keep the two base images on
# the same Python minor + Debian release or the venv's interpreter symlink breaks.
ENV PYTHONUNBUFFERED=1 \
    PYTHONDONTWRITEBYTECODE=1 \
    VIRTUAL_ENV=/app/.venv \
    PATH="/app/.venv/bin:$PATH" \
    HOME=/home/app

# libportaudio2 backs `sounddevice` and libsndfile1 backs `soxr` (both pulled in
# by needle's [mic] extra) for live microphone capture. The file-based flow
# behind /jarvis/ask never opens an audio device, but they are small and keep
# the declared extras working inside the image.
RUN apt-get update \
 && apt-get install -y --no-install-recommends libportaudio2 libsndfile1 \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --create-home --uid 1000 app

WORKDIR /app

COPY --from=builder /app/.venv /app/.venv
# uv installs the project in editable mode, so /app/src must exist at runtime
# at the *same path* it was built at. Do not move these two.
COPY --chown=app:app --from=builder /app/src /app/src
COPY --chown=app:app --from=builder /app/.cache/cactus-needle /home/app/.cache/cactus-needle

# utils.py reads and writes response.wav relative to the working directory, so
# /app itself must be writable by the unprivileged user. The venv stays
# root-owned and read-only; PYTHONDONTWRITEBYTECODE (above) is what keeps
# imports from trying to write __pycache__ into it.
RUN chown app:app /app

USER app

EXPOSE 8000

# Generous start period: importing needle + litellm + elevenlabs and loading the
# engine weights takes a few seconds on first boot.
HEALTHCHECK --interval=30s --timeout=5s --start-period=60s --retries=3 \
    CMD python -c "import urllib.request as u,sys; sys.exit(0 if u.urlopen('http://127.0.0.1:8000/health',timeout=4).status==200 else 1)"

CMD ["uvicorn", "jarvis_iot.server:app", "--host", "0.0.0.0", "--port", "8000"]
