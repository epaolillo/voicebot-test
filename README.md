# Piper Voicebot

Voice assistant that captures microphone input, transcribes speech with OpenAI Whisper, generates responses with an LLM, and speaks them back using Piper TTS.

## Build

```bash
# Install system dependencies
sudo apt install build-essential libasound2-dev libcurl4-openssl-dev cmake libmicrohttpd-dev

# Download Piper, voice model, RNNoise, whisper.cpp, and Whisper model
bash setup.sh

# Compile
make
```

## Run

```bash
# Using OpenAI Whisper API for STT
./build/voicebot --openai <key> --company <name> --name <first> --lastname <last>

# Using local whisper.cpp for STT (no network latency)
./build/voicebot --openai <key> --company <name> --name <first> --lastname <last> --whisper-local
```

| Flag | Required | Description |
|------|----------|-------------|
| `--openai <key>` | Yes | OpenAI API key (always needed for LLM) |
| `--company <name>` | Yes | Company name for greeting |
| `--name <first>` | Yes | Caller's first name |
| `--lastname <last>` | Yes | Caller's last name |
| `--whisper-local [path]` | No | Use local whisper.cpp STT (default model in `config.h`: `WHISPER_LOCAL_MODEL`) |
| `--fillers` | No | Enable thinking/backchannel fillers during conversation |
| `--autolistening` | No | Keep mic active during TTS playback |

## TTS API mode (OpenAI-compatible)

Runs an HTTP server using **only Piper** (no mic, Whisper, or LLM). Endpoints mirror OpenAI’s audio API shape:

- `GET /v1/models` — lists voices (`models/*.onnx` stems as `id`)
- `POST /v1/audio/speech` — JSON body: `input`, optional `voice` / `model`, `response_format` (`pcm` or `wav`), `speed`, `stream`, `stream_format` (`audio` or `sse`)

```bash
./build/voicebot --api --listen 8080
# optional: --api-key <secret>  → require  Authorization: Bearer <secret>
```

Examples:

```bash
curl -s http://127.0.0.1:8080/v1/models | jq .

curl -s -X POST http://127.0.0.1:8080/v1/audio/speech \
  -H "Content-Type: application/json" \
  -d '{"input":"Hola","voice":"es_AR-daniela-high","response_format":"pcm","stream":true,"stream_format":"audio"}' \
  --output out.pcm
```

PCM is s16le mono; sample rate is in the `X-Sample-Rate` header (from the voice `.json`). For `stream_format":"sse"`, each event is `data: <base64>` lines; stream ends with `data: [DONE]`.

If `model` is a generic OpenAI TTS name (`tts-1`, `gpt-4o-mini-tts`, …), the first local voice is used. `mp3` / `opus` are not supported.
