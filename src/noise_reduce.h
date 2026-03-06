#ifndef NOISE_REDUCE_H
#define NOISE_REDUCE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize RNNoise denoiser.
 * Returns 0 on success, -1 on error.
 */
int noise_reduce_init(void);

/**
 * Process a frame of 480 samples (10ms at 48kHz) in-place.
 * Returns the VAD probability from RNNoise [0.0, 1.0].
 */
float noise_reduce_process(int16_t *frame, int frame_size);

/**
 * Free RNNoise resources.
 */
void noise_reduce_free(void);

#ifdef __cplusplus
}
#endif

#endif /* NOISE_REDUCE_H */
