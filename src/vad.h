#ifndef VAD_H
#define VAD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VAD_SILENCE,
    VAD_SPEECH,
    VAD_SPEECH_END
} VadState;

/**
 * Reset VAD state and begin background noise calibration.
 */
void vad_reset(void);

/**
 * Feed a frame of audio samples to the VAD.
 * Returns the current state after processing this frame.
 *
 * @param samples  PCM S16 samples
 * @param count    Number of samples in the frame
 * @param rnnoise_vad  VAD probability from RNNoise (0.0-1.0), used as hint
 */
VadState vad_process(const int16_t *samples, int count, float rnnoise_vad);

#ifdef __cplusplus
}
#endif

#endif /* VAD_H */
