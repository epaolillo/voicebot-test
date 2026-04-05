#include "whisper_local.h"
#include "config.h"

#include "whisper.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static struct whisper_context *ctx = nullptr;

int whisper_local_init(const char *model_path)
{
    if (!model_path || model_path[0] == '\0') {
        fprintf(stderr, "[whisper-local] No model path provided\n");
        return -1;
    }

    whisper_context_params cparams = whisper_context_default_params();
    ctx = whisper_init_from_file_with_params(model_path, cparams);
    if (!ctx) {
        fprintf(stderr, "[whisper-local] Failed to load model: %s\n", model_path);
        return -1;
    }

    fprintf(stderr, "[whisper-local] Model loaded: %s\n", model_path);
    return 0;
}

int whisper_local_transcribe(const int16_t *samples, int sample_count, char **out_text)
{
    if (!ctx || !samples || sample_count <= 0 || !out_text)
        return -1;

    std::vector<float> pcmf32(sample_count);
    for (int i = 0; i < sample_count; i++)
        pcmf32[i] = (float)samples[i] / 32768.0f;

    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_progress   = false;
    wparams.print_special    = false;
    wparams.print_realtime   = false;
    wparams.print_timestamps = false;
    wparams.single_segment   = false;
    wparams.language         = WHISPER_LOCAL_LANGUAGE;
    wparams.n_threads        = WHISPER_LOCAL_THREADS;

    if (whisper_full(ctx, wparams, pcmf32.data(), (int)pcmf32.size()) != 0) {
        fprintf(stderr, "[whisper-local] Inference failed\n");
        return -1;
    }

    int n_segments = whisper_full_n_segments(ctx);
    if (n_segments <= 0) {
        *out_text = strdup("");
        return 0;
    }

    size_t total_len = 0;
    for (int i = 0; i < n_segments; i++)
        total_len += strlen(whisper_full_get_segment_text(ctx, i));

    char *result = (char *)malloc(total_len + 1);
    if (!result)
        return -1;

    result[0] = '\0';
    for (int i = 0; i < n_segments; i++)
        strcat(result, whisper_full_get_segment_text(ctx, i));

    /* Strip leading/trailing whitespace */
    char *start = result;
    while (*start == ' ' || *start == '\n' || *start == '\r')
        start++;

    char *end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\n' || *end == '\r'))
        *end-- = '\0';

    if (start != result)
        memmove(result, start, strlen(start) + 1);

    *out_text = result;
    return 0;
}

void whisper_local_free(void)
{
    if (ctx) {
        whisper_free(ctx);
        ctx = nullptr;
        fprintf(stderr, "[whisper-local] Cleaned up\n");
    }
}
