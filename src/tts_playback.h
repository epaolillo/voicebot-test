#ifndef TTS_PLAYBACK_H
#define TTS_PLAYBACK_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Auto-discover Piper binary, espeak-ng-data, and voice model.
 * Searches in piper/ and models/ directories relative to base_dir.
 * Returns 0 if everything was found, -1 on error.
 */
int tts_auto_discover(const char *base_dir);

/**
 * Manually configure Piper TTS paths (overrides auto-discover).
 * Any NULL parameter keeps the current value.
 */
void tts_configure(const char *piper_bin, const char *model_path,
                   const char *espeak_data);

/**
 * Synthesize text and play it through the speakers.
 * Blocks until playback finishes.
 * Returns 0 on success, -1 on error.
 */
int tts_speak(const char *text);

/**
 * Returns 1 if TTS is currently playing audio, 0 otherwise.
 */
int tts_is_speaking(void);

/**
 * Return the configured sample rate read from the model's JSON config.
 */
int tts_get_sample_rate(void);

/** Paths after tts_auto_discover (for API / synthesis). May return "". */
const char *tts_get_piper_bin(void);
const char *tts_get_piper_dir(void);
const char *tts_get_espeak_data(void);
/** Full path to the first discovered .onnx model. */
const char *tts_get_model_path(void);

#ifdef __cplusplus
}
#endif

#endif /* TTS_PLAYBACK_H */
