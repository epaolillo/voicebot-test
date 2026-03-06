#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Open the default ALSA capture device.
 * Returns 0 on success, negative on error.
 */
int audio_capture_open(void);

/**
 * Read exactly `frames` samples into `buffer`.
 * Blocks until data is available.
 * Returns number of frames read, or negative on error.
 */
int audio_capture_read(int16_t *buffer, int frames);

/**
 * Drop all buffered audio and reset the capture stream.
 * Call after TTS playback to discard any self-recorded audio.
 */
void audio_capture_flush(void);

/**
 * Close the capture device and free resources.
 */
void audio_capture_close(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_CAPTURE_H */
