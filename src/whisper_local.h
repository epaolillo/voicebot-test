#ifndef WHISPER_LOCAL_H
#define WHISPER_LOCAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Load a whisper.cpp GGML model from disk.
 * @param model_path  Path to ggml-*.bin file
 * @return 0 on success, -1 on error
 */
int whisper_local_init(const char *model_path);

/**
 * Transcribe raw PCM S16_LE audio at 16kHz mono.
 *
 * @param samples      PCM S16_LE samples at 16kHz
 * @param sample_count Number of samples
 * @param out_text     Receives malloc'd transcription string (caller must free)
 * @return 0 on success, -1 on error
 */
int whisper_local_transcribe(const int16_t *samples, int sample_count, char **out_text);

/**
 * Cleanup whisper.cpp resources.
 */
void whisper_local_free(void);

#ifdef __cplusplus
}
#endif

#endif /* WHISPER_LOCAL_H */
