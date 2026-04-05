#!/bin/bash
set -e

PIPER_VERSION="2023.11.14-2"
PIPER_URL="https://github.com/rhasspy/piper/releases/download/${PIPER_VERSION}/piper_linux_x86_64.tar.gz"

# Hugging Face rhasspy/piper-voices layout: <lang>/<locale>/<speaker>/<quality>/<voice_id>.onnx
PIPER_VOICES_REV="v1.0.0"
PIPER_VOICES_BASE="https://huggingface.co/rhasspy/piper-voices/resolve/${PIPER_VOICES_REV}"

# Default voices: relative path under piper-voices repo | voice id (stem of .onnx)
# Override with env PIPER_VOICE_SPECS="path|id path2|id2 ..."
DEFAULT_VOICE_SPECS="es/es_AR/daniela/high|es_AR-daniela-high es/es_MX/claude/high|es_MX-claude-high"
PIPER_VOICE_SPECS="${PIPER_VOICE_SPECS:-$DEFAULT_VOICE_SPECS}"

WHISPER_MODEL_NAME="ggml-large-v3-turbo.bin"
WHISPER_MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/${WHISPER_MODEL_NAME}"

# Set SKIP_WHISPER_MODEL=1 for API-only / smaller Docker images (~1.5GB saved)
SKIP_WHISPER_MODEL="${SKIP_WHISPER_MODEL:-0}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== Piper Voicebot Setup ==="

# --- Download Piper ---
if [ ! -f piper/piper ]; then
    echo "[1/5] Downloading Piper TTS..."
    mkdir -p piper
    curl -L "$PIPER_URL" | tar xz -C piper --strip-components=1
    chmod +x piper/piper
    echo "      Done: piper/piper"
else
    echo "[1/5] Piper already installed"
fi

# --- Download Piper voice models (.onnx + .json) ---
echo "[2/5] Piper voice models..."
mkdir -p models
for spec in $PIPER_VOICE_SPECS; do
    rel="${spec%%|*}"
    vid="${spec##*|}"
    if [ -z "$rel" ] || [ -z "$vid" ] || [ "$rel" = "$spec" ]; then
        echo "ERROR: bad PIPER_VOICE_SPECS entry (want 'path|voice_id'): $spec"
        exit 1
    fi
    ONNX_FILE="models/${vid}.onnx"
    if [ -f "$ONNX_FILE" ] && [ "$(stat -c%s "$ONNX_FILE" 2>/dev/null)" -ge 1000 ]; then
        echo "      skip (present): $ONNX_FILE"
        continue
    fi
    echo "      downloading: $vid ..."
    VOICE_ONNX_URL="${PIPER_VOICES_BASE}/${rel}/${vid}.onnx?download=true"
    VOICE_JSON_URL="${PIPER_VOICES_BASE}/${rel}/${vid}.onnx.json?download=true"
    curl -L "$VOICE_ONNX_URL" -o "$ONNX_FILE"
    curl -L "$VOICE_JSON_URL" -o "${ONNX_FILE}.json"
    SIZE=$(stat -c%s "$ONNX_FILE" 2>/dev/null || echo 0)
    if [ "$SIZE" -lt 1000 ]; then
        echo "ERROR: Download failed for $vid (file too small: ${SIZE} bytes)"
        rm -f "$ONNX_FILE" "${ONNX_FILE}.json"
        exit 1
    fi
    echo "      done: $ONNX_FILE ($(numfmt --to=iec $SIZE 2>/dev/null || echo "${SIZE} B"))"
done

# --- Download RNNoise vendor ---
if [ ! -f vendor/rnnoise/src/rnnoise_data.h ]; then
    echo "[3/5] Setting up RNNoise..."
    git clone https://gitlab.xiph.org/xiph/rnnoise.git vendor/rnnoise 2>/dev/null || true
    cd vendor/rnnoise && bash download_model.sh && cd ../..
    echo "      Done"
else
    echo "[3/5] RNNoise already set up"
fi

# --- Clone whisper.cpp ---
if [ ! -f vendor/whisper.cpp/CMakeLists.txt ]; then
    echo "[4/5] Cloning whisper.cpp..."
    git clone --depth 1 https://github.com/ggerganov/whisper.cpp.git vendor/whisper.cpp
    echo "      Done"
else
    echo "[4/5] whisper.cpp already cloned"
fi

# --- Download Whisper GGML model (optional) ---
WHISPER_MODEL_FILE="models/${WHISPER_MODEL_NAME}"
if [ "$SKIP_WHISPER_MODEL" = "1" ] || [ "$SKIP_WHISPER_MODEL" = "true" ]; then
    echo "[5/5] Skipping Whisper model (SKIP_WHISPER_MODEL=$SKIP_WHISPER_MODEL) — use for --api-only or add model later"
elif [ ! -f "$WHISPER_MODEL_FILE" ] || [ "$(stat -c%s "$WHISPER_MODEL_FILE" 2>/dev/null)" -lt 1000 ]; then
    echo "[5/5] Downloading Whisper model (large-v3-turbo, ~1.5GB)..."
    mkdir -p models
    curl -L "$WHISPER_MODEL_URL" -o "$WHISPER_MODEL_FILE"
    SIZE=$(stat -c%s "$WHISPER_MODEL_FILE" 2>/dev/null || echo 0)
    if [ "$SIZE" -lt 1000 ]; then
        echo "ERROR: Download failed (file too small: ${SIZE} bytes)"
        rm -f "$WHISPER_MODEL_FILE"
        exit 1
    fi
    echo "      Done: $WHISPER_MODEL_FILE ($(numfmt --to=iec $SIZE))"
else
    echo "[5/5] Whisper model already present"
fi

echo ""
echo "=== Setup complete ==="
echo "Now run: make && ./build/voicebot --openai <key> --company <name> --name <first> --lastname <last>"
echo "Add --whisper-local for local STT (requires ${WHISPER_MODEL_NAME} in models/)"
echo "API-only: ./build/voicebot --api --listen 8080"
