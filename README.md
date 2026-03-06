# Piper Voicebot

Voice assistant that captures microphone input, transcribes speech with OpenAI Whisper, generates responses with an LLM, and speaks them back using Piper TTS.

## Build

```bash
# Install system dependencies
sudo apt install build-essential libasound2-dev libcurl4-openssl-dev

# Download Piper, voice model, and RNNoise
bash setup.sh

# Compile
make
```

## Run

```bash
./build/voicebot --openai <key> --company <name> --name <first> --lastname <last>
```

| Flag | Required | Description |
|------|----------|-------------|
| `--openai <key>` | Yes | OpenAI API key |
| `--company <name>` | Yes | Company name for greeting |
| `--name <first>` | Yes | Caller's first name |
| `--lastname <last>` | Yes | Caller's last name |
| `--autolistening` | No | Keep mic active during TTS playback |
