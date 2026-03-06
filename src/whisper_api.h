#ifndef WHISPER_API_H
#define WHISPER_API_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize libcurl for Whisper API calls.
 * @param api_key  OpenAI API key
 * Returns 0 on success, -1 on error.
 */
int whisper_api_init(const char *api_key);

/**
 * Transcribe a WAV buffer using OpenAI Whisper API.
 *
 * @param wav_data  Complete WAV file in memory
 * @param wav_size  Size of WAV data in bytes
 * @param out_text  Receives malloc'd transcription string (caller must free)
 * @return 0 on success, -1 on error
 */
int whisper_api_transcribe(const void *wav_data, size_t wav_size, char **out_text);

/**
 * Cleanup Whisper API resources.
 */
void whisper_api_free(void);

#ifdef __cplusplus
}
#endif

#endif /* WHISPER_API_H */
