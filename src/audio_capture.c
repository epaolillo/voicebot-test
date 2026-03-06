#include "audio_capture.h"
#include "config.h"

#include <alsa/asoundlib.h>
#include <stdio.h>

static snd_pcm_t *pcm_handle = NULL;

int audio_capture_open(void)
{
    int rc;

    rc = snd_pcm_open(&pcm_handle, CAPTURE_DEVICE, SND_PCM_STREAM_CAPTURE, 0);
    if (rc < 0) {
        fprintf(stderr, "[audio] Cannot open device '%s': %s\n",
                CAPTURE_DEVICE, snd_strerror(rc));
        return rc;
    }

    snd_pcm_hw_params_t *params;
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(pcm_handle, params);

    rc = snd_pcm_hw_params_set_access(pcm_handle, params,
                                       SND_PCM_ACCESS_RW_INTERLEAVED);
    if (rc < 0) {
        fprintf(stderr, "[audio] Cannot set access: %s\n", snd_strerror(rc));
        return rc;
    }

    rc = snd_pcm_hw_params_set_format(pcm_handle, params,
                                       SND_PCM_FORMAT_S16_LE);
    if (rc < 0) {
        fprintf(stderr, "[audio] Cannot set format S16_LE: %s\n", snd_strerror(rc));
        return rc;
    }

    rc = snd_pcm_hw_params_set_channels(pcm_handle, params, CAPTURE_CHANNELS);
    if (rc < 0) {
        fprintf(stderr, "[audio] Cannot set channels=%d: %s\n",
                CAPTURE_CHANNELS, snd_strerror(rc));
        return rc;
    }

    unsigned int rate = CAPTURE_SAMPLE_RATE;
    rc = snd_pcm_hw_params_set_rate_near(pcm_handle, params, &rate, 0);
    if (rc < 0) {
        fprintf(stderr, "[audio] Cannot set rate=%u: %s\n", rate, snd_strerror(rc));
        return rc;
    }
    if (rate != CAPTURE_SAMPLE_RATE) {
        fprintf(stderr, "[audio] Rate %u not available, using %u\n",
                CAPTURE_SAMPLE_RATE, rate);
    }

    snd_pcm_uframes_t period = CAPTURE_FRAME_SIZE;
    rc = snd_pcm_hw_params_set_period_size_near(pcm_handle, params, &period, 0);
    if (rc < 0) {
        fprintf(stderr, "[audio] Cannot set period size: %s\n", snd_strerror(rc));
        return rc;
    }

    rc = snd_pcm_hw_params(pcm_handle, params);
    if (rc < 0) {
        fprintf(stderr, "[audio] Cannot apply hw params: %s\n", snd_strerror(rc));
        return rc;
    }

    rc = snd_pcm_prepare(pcm_handle);
    if (rc < 0) {
        fprintf(stderr, "[audio] Cannot prepare device: %s\n", snd_strerror(rc));
        return rc;
    }

    fprintf(stderr, "[audio] Opened '%s' at %uHz, %d channel(s), S16_LE\n",
            CAPTURE_DEVICE, rate, CAPTURE_CHANNELS);
    return 0;
}

int audio_capture_read(int16_t *buffer, int frames)
{
    if (!pcm_handle)
        return -1;

    snd_pcm_sframes_t n = snd_pcm_readi(pcm_handle, buffer, (snd_pcm_uframes_t)frames);

    if (n == -EPIPE) {
        fprintf(stderr, "[audio] Overrun, recovering\n");
        snd_pcm_prepare(pcm_handle);
        n = snd_pcm_readi(pcm_handle, buffer, (snd_pcm_uframes_t)frames);
    }

    if (n < 0) {
        fprintf(stderr, "[audio] Read error: %s\n", snd_strerror((int)n));
        return (int)n;
    }

    return (int)n;
}

void audio_capture_flush(void)
{
    if (pcm_handle) {
        snd_pcm_drop(pcm_handle);
        snd_pcm_prepare(pcm_handle);
    }
}

void audio_capture_close(void)
{
    if (pcm_handle) {
        snd_pcm_drop(pcm_handle);
        snd_pcm_close(pcm_handle);
        pcm_handle = NULL;
        fprintf(stderr, "[audio] Device closed\n");
    }
}
