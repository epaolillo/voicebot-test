#include "vad.h"
#include "config.h"

#include <math.h>
#include <stdio.h>

typedef struct {
    float noise_floor;
    int   calibration_frames;
    int   calibration_target;
    float calibration_sum;
    int   speech_frames;
    int   silence_frames;
    int   in_speech;
    int   speech_start_frames;
} VadContext;

static VadContext ctx;

void vad_reset(void)
{
    ctx.noise_floor = 0.0f;
    ctx.calibration_frames = 0;
    /* How many frames needed for calibration (at 48kHz, 480 samples = 10ms) */
    ctx.calibration_target = (VAD_CALIBRATION_MS * CAPTURE_SAMPLE_RATE)
                             / (RNNOISE_FRAME_SIZE * 1000);
    ctx.calibration_sum = 0.0f;
    ctx.speech_frames = 0;
    ctx.silence_frames = 0;
    ctx.in_speech = 0;
    ctx.speech_start_frames = (VAD_MIN_SPEECH_MS * CAPTURE_SAMPLE_RATE)
                              / (RNNOISE_FRAME_SIZE * 1000);

    fprintf(stderr, "[vad] Reset, calibrating for %d frames\n",
            ctx.calibration_target);
}

static float compute_rms(const int16_t *samples, int count)
{
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        double s = (double)samples[i];
        sum += s * s;
    }
    return (float)sqrt(sum / count);
}

VadState vad_process(const int16_t *samples, int count, float rnnoise_vad)
{
    float rms = compute_rms(samples, count);

    /* Calibration phase: measure background noise */
    if (ctx.calibration_frames < ctx.calibration_target) {
        ctx.calibration_sum += rms;
        ctx.calibration_frames++;
        if (ctx.calibration_frames == ctx.calibration_target) {
            ctx.noise_floor = ctx.calibration_sum / ctx.calibration_target;
            if (ctx.noise_floor < 50.0f)
                ctx.noise_floor = 50.0f;
            fprintf(stderr, "[vad] Calibrated noise floor: %.1f\n", ctx.noise_floor);
        }
        return VAD_SILENCE;
    }

    float threshold = ctx.noise_floor * VAD_THRESHOLD_FACTOR;
    int is_speech = (rms > threshold) || (rnnoise_vad > 0.6f);

    if (!ctx.in_speech) {
        if (is_speech) {
            ctx.speech_frames++;
            ctx.silence_frames = 0;
            if (ctx.speech_frames >= ctx.speech_start_frames) {
                ctx.in_speech = 1;
                return VAD_SPEECH;
            }
        } else {
            ctx.speech_frames = 0;
        }
        return VAD_SILENCE;
    }

    /* Currently in speech */
    if (is_speech) {
        ctx.silence_frames = 0;
        return VAD_SPEECH;
    }

    ctx.silence_frames++;
    int silence_needed = (VAD_SILENCE_MS * CAPTURE_SAMPLE_RATE)
                         / (RNNOISE_FRAME_SIZE * 1000);

    if (ctx.silence_frames >= silence_needed) {
        ctx.in_speech = 0;
        ctx.speech_frames = 0;
        ctx.silence_frames = 0;
        return VAD_SPEECH_END;
    }

    /* Still in trailing silence, consider it speech for buffering */
    return VAD_SPEECH;
}
