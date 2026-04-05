#include "tts_playback.h"
#include "config.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char piper_bin[1024]   = {0};
static char piper_dir[1024]   = {0};
static char model_path[1024]  = {0};
static char espeak_data[1024] = {0};
static int  sample_rate      = PIPER_SAMPLE_RATE;
static volatile int speaking = 0;

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int read_sample_rate_from_config(const char *config_path)
{
    FILE *f = fopen(config_path, "r");
    if (!f)
        return PIPER_SAMPLE_RATE;

    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    /* Minimal JSON parse: find "sample_rate": <number> */
    const char *key = "\"sample_rate\"";
    char *p = strstr(buf, key);
    if (!p)
        return PIPER_SAMPLE_RATE;

    p += strlen(key);
    while (*p == ' ' || *p == ':' || *p == '\t')
        p++;

    int sr = atoi(p);
    return sr > 0 ? sr : PIPER_SAMPLE_RATE;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
static int find_first_onnx(const char *dir, char *out, size_t out_size)
{
    DIR *d = opendir(dir);
    if (!d)
        return -1;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t len = strlen(ent->d_name);
        if (len > 5 && strcmp(ent->d_name + len - 5, ".onnx") == 0) {
            snprintf(out, out_size, "%s/%s", dir, ent->d_name);
            closedir(d);
            return 0;
        }
    }

    closedir(d);
    return -1;
}
#pragma GCC diagnostic pop

int tts_auto_discover(const char *base_dir)
{
    char path[1024];
    int found = 0;

    /* Look for piper binary */
    snprintf(path, sizeof(path), "%s/piper/piper", base_dir);
    if (file_exists(path)) {
        snprintf(piper_bin, sizeof(piper_bin), "%s", path);
        snprintf(piper_dir, sizeof(piper_dir), "%s/piper", base_dir);
        found++;
    }

    /* Look for espeak-ng-data next to the piper binary */
    snprintf(path, sizeof(path), "%s/piper/espeak-ng-data", base_dir);
    if (file_exists(path)) {
        snprintf(espeak_data, sizeof(espeak_data), "%s", path);
        found++;
    }

    /* Look for first .onnx model in models/ */
    snprintf(path, sizeof(path), "%s/models", base_dir);
    if (find_first_onnx(path, model_path, sizeof(model_path)) == 0) {
        found++;

        char config[1030];
        snprintf(config, sizeof(config), "%s.json", model_path);
        if (file_exists(config))
            sample_rate = read_sample_rate_from_config(config);
    }

    if (piper_bin[0] != '\0')
        fprintf(stderr, "[tts] Binary:  %s\n", piper_bin);
    if (model_path[0] != '\0')
        fprintf(stderr, "[tts] Model:   %s (sample_rate=%d)\n", model_path, sample_rate);
    if (espeak_data[0] != '\0')
        fprintf(stderr, "[tts] Espeak:  %s\n", espeak_data);

    if (piper_bin[0] == '\0') {
        fprintf(stderr, "[tts] ERROR: piper binary not found in %s/piper/\n", base_dir);
        fprintf(stderr, "[tts] Run: bash setup.sh\n");
        return -1;
    }
    if (model_path[0] == '\0') {
        fprintf(stderr, "[tts] ERROR: no .onnx model found in %s/models/\n", base_dir);
        fprintf(stderr, "[tts] Run: bash setup.sh\n");
        return -1;
    }

    return 0;
}

void tts_configure(const char *bin, const char *model, const char *espeak)
{
    if (bin && bin[0] != '\0')
        snprintf(piper_bin, sizeof(piper_bin), "%s", bin);
    if (model && model[0] != '\0')
        snprintf(model_path, sizeof(model_path), "%s", model);
    if (espeak && espeak[0] != '\0')
        snprintf(espeak_data, sizeof(espeak_data), "%s", espeak);
}

int tts_is_speaking(void)
{
    return speaking;
}

int tts_get_sample_rate(void)
{
    return sample_rate;
}

static void shell_escape(char *out, size_t out_size, const char *in)
{
    size_t pos = 0;
    out[pos++] = '\'';
    for (const char *p = in; *p && pos + 5 < out_size; p++) {
        if (*p == '\'') {
            memcpy(out + pos, "'\\''", 4);
            pos += 4;
        } else {
            out[pos++] = *p;
        }
    }
    out[pos++] = '\'';
    out[pos] = '\0';
}

int tts_speak(const char *text)
{
    if (!text || text[0] == '\0')
        return 0;

    if (model_path[0] == '\0') {
        fprintf(stderr, "[tts] No model configured\n");
        return -1;
    }

    char escaped_text[4096];
    shell_escape(escaped_text, sizeof(escaped_text), text);

    char escaped_bin[600];
    shell_escape(escaped_bin, sizeof(escaped_bin), piper_bin);

    char escaped_model[600];
    shell_escape(escaped_model, sizeof(escaped_model), model_path);

    char escaped_dir[600] = {0};
    if (piper_dir[0] != '\0')
        shell_escape(escaped_dir, sizeof(escaped_dir), piper_dir);

    char cmd[8192];
    int n;

    if (espeak_data[0] != '\0') {
        char escaped_espeak[600];
        shell_escape(escaped_espeak, sizeof(escaped_espeak), espeak_data);

        n = snprintf(cmd, sizeof(cmd),
            "echo %s | LD_LIBRARY_PATH=%s %s --model %s --config %s.json"
            " --espeak_data %s --length_scale %.2f --sentence_silence %.2f"
            " --output_raw --quiet 2>/dev/null"
            " | aplay -r %d -f S16_LE -c 1 -t raw -q 2>/dev/null",
            escaped_text, escaped_dir, escaped_bin, escaped_model, escaped_model,
            escaped_espeak, (double)PIPER_LENGTH_SCALE, (double)PIPER_SENTENCE_SILENCE,
            sample_rate);
    } else {
        n = snprintf(cmd, sizeof(cmd),
            "echo %s | LD_LIBRARY_PATH=%s %s --model %s --config %s.json"
            " --length_scale %.2f --sentence_silence %.2f"
            " --output_raw --quiet 2>/dev/null"
            " | aplay -r %d -f S16_LE -c 1 -t raw -q 2>/dev/null",
            escaped_text, escaped_dir, escaped_bin, escaped_model, escaped_model,
            (double)PIPER_LENGTH_SCALE, (double)PIPER_SENTENCE_SILENCE,
            sample_rate);
    }

    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        fprintf(stderr, "[tts] Command too long\n");
        return -1;
    }

    speaking = 1;
    int rc = system(cmd);
    speaking = 0;

    if (rc != 0) {
        fprintf(stderr, "[tts] Playback failed (exit=%d)\n", rc);
        return -1;
    }

    return 0;
}
