#include "wav_utils.h"

#include <stdlib.h>
#include <string.h>

static void write16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void write32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

int wav_build(const int16_t *samples, int sample_count,
              int sample_rate, int channels,
              uint8_t **out_data, size_t *out_size)
{
    if (!samples || sample_count <= 0 || !out_data || !out_size)
        return -1;

    uint32_t data_size = (uint32_t)(sample_count * channels * 2);
    uint32_t file_size = 44 + data_size;

    uint8_t *buf = (uint8_t *)malloc(file_size);
    if (!buf)
        return -1;

    /* RIFF header */
    memcpy(buf, "RIFF", 4);
    write32(buf + 4, file_size - 8);
    memcpy(buf + 8, "WAVE", 4);

    /* fmt chunk */
    memcpy(buf + 12, "fmt ", 4);
    write32(buf + 16, 16);                                  /* subchunk size */
    write16(buf + 20, 1);                                   /* PCM format */
    write16(buf + 22, (uint16_t)channels);
    write32(buf + 24, (uint32_t)sample_rate);
    write32(buf + 28, (uint32_t)(sample_rate * channels * 2)); /* byte rate */
    write16(buf + 32, (uint16_t)(channels * 2));            /* block align */
    write16(buf + 34, 16);                                  /* bits per sample */

    /* data chunk */
    memcpy(buf + 36, "data", 4);
    write32(buf + 40, data_size);
    memcpy(buf + 44, samples, data_size);

    *out_data = buf;
    *out_size = file_size;
    return 0;
}
