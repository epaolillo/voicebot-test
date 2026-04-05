# Piper Voicebot

Voice assistant that captures microphone input, transcribes speech with OpenAI Whisper, generates responses with an LLM, and speaks them back using Piper TTS.

## Build

```bash
# Install system dependencies
sudo apt install build-essential libasound2-dev libcurl4-openssl-dev cmake

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
| `--whisper-local [path]` | No | Use local whisper.cpp STT (default model: `models/ggml-small.bin`) |
| `--fillers` | No | Enable thinking/backchannel fillers during conversation |
| `--autolistening` | No | Keep mic active during TTS playback |
