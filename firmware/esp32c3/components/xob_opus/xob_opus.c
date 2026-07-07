#include "xob_opus.h"

#include <stdlib.h>

#include "esp_log.h"
#include "opus.h"

struct xob_opus_decoder {
    OpusDecoder *decoder;
};

static const char *TAG = "xob_opus";

esp_err_t xob_opus_decoder_create(int sample_rate, int channels, xob_opus_decoder_t **out) {
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;

    xob_opus_decoder_t *wrapper = calloc(1, sizeof(*wrapper));
    if (wrapper == NULL) {
        return ESP_ERR_NO_MEM;
    }

    int error = OPUS_OK;
    wrapper->decoder = opus_decoder_create(sample_rate, channels, &error);
    if (wrapper->decoder == NULL || error != OPUS_OK) {
        ESP_LOGW(TAG, "opus decoder create failed error=%d", error);
        free(wrapper);
        return ESP_FAIL;
    }

    *out = wrapper;
    return ESP_OK;
}

void xob_opus_decoder_destroy(xob_opus_decoder_t *decoder) {
    if (decoder == NULL) {
        return;
    }
    if (decoder->decoder != NULL) {
        opus_decoder_destroy(decoder->decoder);
    }
    free(decoder);
}

esp_err_t xob_opus_decoder_reset(xob_opus_decoder_t *decoder) {
    if (decoder == NULL || decoder->decoder == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int ret = opus_decoder_ctl(decoder->decoder, OPUS_RESET_STATE);
    return ret == OPUS_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t xob_opus_decode(
    xob_opus_decoder_t *decoder,
    const uint8_t *opus,
    size_t opus_len,
    int16_t *pcm,
    size_t pcm_capacity_samples,
    size_t *pcm_samples
) {
    if (decoder == NULL || decoder->decoder == NULL || opus == NULL || opus_len == 0 ||
        pcm == NULL || pcm_capacity_samples == 0 || pcm_samples == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int decoded = opus_decode(
        decoder->decoder,
        opus,
        (opus_int32)opus_len,
        pcm,
        (int)pcm_capacity_samples,
        0
    );
    if (decoded < 0) {
        ESP_LOGW(TAG, "opus decode failed error=%d len=%u", decoded, (unsigned)opus_len);
        return ESP_FAIL;
    }

    *pcm_samples = (size_t)decoded;
    return ESP_OK;
}
