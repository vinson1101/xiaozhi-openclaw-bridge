#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct xob_opus_decoder xob_opus_decoder_t;

esp_err_t xob_opus_decoder_create(int sample_rate, int channels, xob_opus_decoder_t **out);
void xob_opus_decoder_destroy(xob_opus_decoder_t *decoder);
esp_err_t xob_opus_decoder_reset(xob_opus_decoder_t *decoder);
esp_err_t xob_opus_decode(
    xob_opus_decoder_t *decoder,
    const uint8_t *opus,
    size_t opus_len,
    int16_t *pcm,
    size_t pcm_capacity_samples,
    size_t *pcm_samples
);
