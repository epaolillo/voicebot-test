#include "noise_reduce.h"
#include "config.h"

#include <rnnoise.h>
#include <stdio.h>

static DenoiseState *rnn_state = NULL;

int noise_reduce_init(void)
{
    rnn_state = rnnoise_create(NULL);
    if (!rnn_state) {
        fprintf(stderr, "[denoise] Failed to create RNNoise state\n");
        return -1;
    }
    fprintf(stderr, "[denoise] RNNoise initialized (frame_size=%d)\n",
            RNNOISE_FRAME_SIZE);
    return 0;
}

float noise_reduce_process(int16_t *frame, int frame_size)
{
    if (!rnn_state || frame_size != RNNOISE_FRAME_SIZE)
        return 0.0f;

    /* RNNoise expects float in [-32768, 32767] range */
    float fbuf[RNNOISE_FRAME_SIZE];
    for (int i = 0; i < RNNOISE_FRAME_SIZE; i++)
        fbuf[i] = (float)frame[i];

    float vad_prob = rnnoise_process_frame(rnn_state, fbuf, fbuf);

    for (int i = 0; i < RNNOISE_FRAME_SIZE; i++) {
        float v = fbuf[i];
        if (v > 32767.0f) v = 32767.0f;
        if (v < -32768.0f) v = -32768.0f;
        frame[i] = (int16_t)v;
    }

    return vad_prob;
}

void noise_reduce_free(void)
{
    if (rnn_state) {
        rnnoise_destroy(rnn_state);
        rnn_state = NULL;
        fprintf(stderr, "[denoise] RNNoise freed\n");
    }
}
