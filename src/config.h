#ifndef CONFIG_H
#define CONFIG_H

/* ALSA capture settings */
#define CAPTURE_DEVICE       "default"
#define CAPTURE_SAMPLE_RATE  48000
#define CAPTURE_CHANNELS     1
#define CAPTURE_FRAME_MS     10
#define CAPTURE_FRAME_SIZE   (CAPTURE_SAMPLE_RATE * CAPTURE_FRAME_MS / 1000)

/* Downsampled rate for Whisper API (16kHz expected) */
#define WHISPER_SAMPLE_RATE  16000
#define DOWNSAMPLE_RATIO     (CAPTURE_SAMPLE_RATE / WHISPER_SAMPLE_RATE)

/* RNNoise operates on 480-sample frames at 48kHz */
#define RNNOISE_FRAME_SIZE   480

/* VAD settings */
#define VAD_CALIBRATION_MS   500
#define VAD_SILENCE_MS       800
#define VAD_THRESHOLD_FACTOR 3.0f
#define VAD_MIN_SPEECH_MS    200

/* Whisper API (remote) */
#define WHISPER_API_URL      "https://api.openai.com/v1/audio/transcriptions"
#define WHISPER_MODEL        "gpt-4o-mini-transcribe"

/* Whisper local (whisper.cpp) */
#define WHISPER_LOCAL_MODEL   "models/ggml-tiny.bin"
#define WHISPER_LOCAL_LANGUAGE "es"
#define WHISPER_LOCAL_THREADS  4

/* LLM settings */
#define LLM_API_URL          "https://api.openai.com/v1/chat/completions"
#define LLM_MODEL            "gpt-4.1-nano"
#define LLM_MAX_TOKENS       512
#define LLM_TEMPERATURE      0.7f

#define LLM_SYSTEM_PROMPT_FMT \
    "Sos un asistente de voz argentino. Respondé de forma breve y natural, " \
    "como si estuvieras charlando con alguien. Usá vocabulario rioplatense " \
    "y tuteo con vos. Sé conciso, no más de 2-3 oraciones por respuesta. " \
    "Tu objetivo final es intentar cobrar una deuda que %s %s tiene con %s. " \
    "Intentá ser lo mas persuasiva posible. Sé formal, pero amable."

/* Piper TTS */
#define PIPER_BINARY         "piper"
#define PIPER_MODEL          ""
#define PIPER_SAMPLE_RATE    22050
#define PIPER_ESPEAK_DATA    ""
#define PIPER_LENGTH_SCALE   0.85f
#define PIPER_SENTENCE_SILENCE 0.0f

/* Audio recording buffer: max 30 seconds at 16kHz */
#define MAX_RECORDING_SAMPLES (WHISPER_SAMPLE_RATE * 30)

/*
 * When 0, mic input is muted while TTS is playing to prevent
 * the bot from hearing its own output. Set to 1 to keep the mic
 * active during playback (requires echo cancellation or headphones).
 */
#define DEFAULT_AUTOLISTENING 0

/* Pause before thinking fillers (ms) to let Whisper/LLM start processing */
#define FILLER_DELAY_MS 700

#endif /* CONFIG_H */
