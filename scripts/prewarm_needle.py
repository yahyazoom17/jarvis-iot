"""Pre-download the Needle engine and its model weights.

`needle` resolves these lazily on first use: a native `libneedle.so` plus the
`needle3.cact` / `whistle.cact` archives, fetched from Hugging Face into
``$HOME/.cache/cactus-needle/``. Running this during the image build means the
container starts instantly and never needs network access or an ``HF_TOKEN``
at runtime.

Warm-cache calls are no-ops, so it is safe to re-run.
"""

from needle.agent import fetch

# Generation 3 is the agent engine; WHISTLE is the speech-to-text engine used by
# needle.transcribe(). Both resolve through fetch.cache_dir(), which is rooted at
# $HOME -- so run this with the same $HOME the runtime user will have.
print("agent weights :", fetch.fetch_weights(3))
print("whisper weights:", fetch.fetch_weights(fetch.WHISTLE))
print(
    "engine library:",
    fetch.fetch_library(
        version=fetch.engine_version(3),
        dest_dir=fetch.cache_dir(3),
        generation=3,
    ),
)
