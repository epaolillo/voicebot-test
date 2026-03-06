#ifndef WAV_UTILS_H
#define WAV_UTILS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Build a complete WAV file in memory from raw PCM S16_LE samples.
 *
 * @param samples      Input PCM data
 * @param sample_count Number of samples
 * @param sample_rate  Sample rate in Hz
 * @param channels     Number of channels (1 = mono)
 * @param out_data     Receives malloc'd buffer (caller must free)
 * @param out_size     Receives total size in bytes
 * @return 0 on success, -1 on error
 */
int wav_build(const int16_t *samples, int sample_count,
              int sample_rate, int channels,
              uint8_t **out_data, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* WAV_UTILS_H */
