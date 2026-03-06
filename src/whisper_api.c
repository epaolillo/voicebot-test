#include "whisper_api.h"
#include "config.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char api_key[256] = {0};

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} Buffer;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    Buffer *buf = (Buffer *)userdata;
    size_t total = size * nmemb;

    if (buf->len + total >= buf->cap) {
        size_t new_cap = (buf->cap + total) * 2;
        char *tmp = (char *)realloc(buf->data, new_cap);
        if (!tmp)
            return 0;
        buf->data = tmp;
        buf->cap = new_cap;
    }

    memcpy(buf->data + buf->len, ptr, total);
    buf->len += total;
    buf->data[buf->len] = '\0';
    return total;
}

int whisper_api_init(const char *key)
{
    if (!key || key[0] == '\0') {
        fprintf(stderr, "[whisper] No API key provided\n");
        return -1;
    }

    snprintf(api_key, sizeof(api_key), "%s", key);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    fprintf(stderr, "[whisper] API initialized (model=%s)\n", WHISPER_MODEL);
    return 0;
}

int whisper_api_transcribe(const void *wav_data, size_t wav_size, char **out_text)
{
    if (!wav_data || wav_size == 0 || !out_text)
        return -1;

    CURL *curl = curl_easy_init();
    if (!curl)
        return -1;

    Buffer response = {0};
    response.cap = 4096;
    response.data = (char *)malloc(response.cap);
    if (!response.data) {
        curl_easy_cleanup(curl);
        return -1;
    }
    response.data[0] = '\0';

    char auth_header[300];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, auth_header);

    curl_mime *mime = curl_mime_init(curl);

    /* file field */
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_data(part, (const char *)wav_data, wav_size);
    curl_mime_filename(part, "audio.wav");
    curl_mime_type(part, "audio/wav");

    /* model field */
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "model");
    curl_mime_data(part, WHISPER_MODEL, CURL_ZERO_TERMINATED);

    /* response_format = text for minimal overhead */
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "response_format");
    curl_mime_data(part, "text", CURL_ZERO_TERMINATED);

    /* language hint */
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "language");
    curl_mime_data(part, "es", CURL_ZERO_TERMINATED);

    curl_easy_setopt(curl, CURLOPT_URL, WHISPER_API_URL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_mime_free(mime);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "[whisper] curl error: %s\n", curl_easy_strerror(res));
        free(response.data);
        return -1;
    }

    if (http_code != 200) {
        fprintf(stderr, "[whisper] HTTP %ld: %s\n", http_code, response.data);
        free(response.data);
        return -1;
    }

    /* Strip trailing whitespace */
    while (response.len > 0 &&
           (response.data[response.len - 1] == '\n' ||
            response.data[response.len - 1] == '\r' ||
            response.data[response.len - 1] == ' ')) {
        response.data[--response.len] = '\0';
    }

    *out_text = response.data;
    return 0;
}

void whisper_api_free(void)
{
    curl_global_cleanup();
    fprintf(stderr, "[whisper] Cleaned up\n");
}
