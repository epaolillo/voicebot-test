#ifndef TTS_SYNTHESIZE_H
#define TTS_SYNTHESIZE_H

#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Fork Piper: stdin = text, stdout = raw PCM s16le.
 * Parent must read out_fd until EOF, then close(out_fd) and waitpid(pid, ...).
 * Returns 0 on success, -1 on error (no child left running).
 */
int tts_synthesize_pcm_stream_start(const char *text, const char *model_full_path,
                                    float length_scale,
                                    const char *piper_bin, const char *piper_ld_path,
                                    const char *espeak_data,
                                    int *out_fd, pid_t *out_pid);

/**
 * Full PCM in memory (for non-streaming WAV). Caller frees *pcm_out.
 */
int tts_synthesize_pcm_collect(const char *text, const char *model_full_path,
                               float length_scale,
                               const char *piper_bin, const char *piper_ld_path,
                               const char *espeak_data,
                               uint8_t **pcm_out, size_t *pcm_size);

#ifdef __cplusplus
}
#endif

#endif /* TTS_SYNTHESIZE_H */
