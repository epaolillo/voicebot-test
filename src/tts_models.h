#ifndef TTS_MODELS_H
#define TTS_MODELS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Build OpenAI-style GET /v1/models JSON listing ONNX voice files under base_dir/models.
 * Caller must free() the returned string on success.
 * Returns NULL on allocation failure or if models dir missing.
 */
char *tts_models_build_json_list(const char *base_dir);

/**
 * Resolve voice id (stem without .onnx) to full path models/<id>.onnx under base_dir.
 * Returns 0 on success, -1 if invalid (path traversal, missing file).
 */
int tts_models_resolve_voice_path(const char *base_dir, const char *voice_id,
                                  char *out_path, size_t out_size);

/**
 * Read sample_rate from model's .onnx.json next to onnx_path. Returns default on failure.
 */
int tts_models_read_sample_rate(const char *onnx_path, int default_rate);

/**
 * Get default voice id (first .onnx stem in models/) into out_id. Returns 0 or -1.
 */
int tts_models_default_voice_id(const char *base_dir, char *out_id, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* TTS_MODELS_H */
