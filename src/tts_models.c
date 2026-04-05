#include "tts_models.h"
#include "config.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int is_safe_voice_id(const char *id)
{
    if (!id || id[0] == '\0')
        return 0;
    for (const char *p = id; *p; p++) {
        if (*p == '/' || *p == '\\' || (*p == '.' && p[1] == '.'))
            return 0;
        if ((unsigned char)*p < 0x20)
            return 0;
    }
    return 1;
}

int tts_models_resolve_voice_path(const char *base_dir, const char *voice_id,
                                  char *out_path, size_t out_size)
{
    if (!base_dir || !voice_id || !out_path || out_size < 16)
        return -1;
    if (!is_safe_voice_id(voice_id))
        return -1;

    char path[2048];
    snprintf(path, sizeof(path), "%s/models/%s.onnx", base_dir, voice_id);
    if (!file_exists(path))
        return -1;

    snprintf(out_path, out_size, "%s", path);
    return 0;
}

int tts_models_read_sample_rate(const char *onnx_path, int default_rate)
{
    char config[2100];
    snprintf(config, sizeof(config), "%s.json", onnx_path);

    FILE *f = fopen(config, "r");
    if (!f)
        return default_rate;

    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    const char *key = "\"sample_rate\"";
    char *p = strstr(buf, key);
    if (!p)
        return default_rate;

    p += strlen(key);
    while (*p == ' ' || *p == ':' || *p == '\t')
        p++;

    int sr = atoi(p);
    return sr > 0 ? sr : default_rate;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
static int collect_first_onnx_id(const char *dir, char *out_id, size_t out_size)
{
    DIR *d = opendir(dir);
    if (!d)
        return -1;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t len = strlen(ent->d_name);
        if (len > 5 && strcmp(ent->d_name + len - 5, ".onnx") == 0) {
            size_t stem = len - 5;
            if (stem >= out_size)
                stem = out_size - 1;
            memcpy(out_id, ent->d_name, stem);
            out_id[stem] = '\0';
            closedir(d);
            return 0;
        }
    }

    closedir(d);
    return -1;
}
#pragma GCC diagnostic pop

int tts_models_default_voice_id(const char *base_dir, char *out_id, size_t out_size)
{
    if (!base_dir || !out_id || out_size == 0)
        return -1;

    char models_dir[1024];
    snprintf(models_dir, sizeof(models_dir), "%s/models", base_dir);
    return collect_first_onnx_id(models_dir, out_id, out_size);
}

char *tts_models_build_json_list(const char *base_dir)
{
    if (!base_dir)
        return NULL;

    char models_dir[1024];
    snprintf(models_dir, sizeof(models_dir), "%s/models", base_dir);

    DIR *d = opendir(models_dir);
    if (!d)
        return NULL;

    size_t cap = 4096;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        closedir(d);
        return NULL;
    }

    size_t len = 0;
    len += (size_t)snprintf(buf + len, cap - len,
        "{\"object\":\"list\",\"data\":[");

    int first = 1;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t nl = strlen(ent->d_name);
        if (nl <= 5 || strcmp(ent->d_name + nl - 5, ".onnx") != 0)
            continue;

        char full[1200];
        snprintf(full, sizeof(full), "%s/%s", models_dir, ent->d_name);

        struct stat st;
        long long created = 0;
        if (stat(full, &st) == 0)
            created = (long long)st.st_mtime;

        char id[256];
        size_t stem = nl - 5;
        if (stem >= sizeof(id))
            stem = sizeof(id) - 1;
        memcpy(id, ent->d_name, stem);
        id[stem] = '\0';

        if (!is_safe_voice_id(id))
            continue;

        char piece[512];
        int pn = snprintf(piece, sizeof(piece),
            "%s{\"id\":\"%s\",\"object\":\"model\",\"created\":%lld,\"owned_by\":\"piper\"}",
            first ? "" : ",", id, created);
        first = 0;

        if (pn < 0 || (size_t)pn >= sizeof(piece))
            continue;

        while (len + (size_t)pn + 8 > cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) {
                free(buf);
                closedir(d);
                return NULL;
            }
            buf = nb;
        }
        memcpy(buf + len, piece, (size_t)pn);
        len += (size_t)pn;
        buf[len] = '\0';
    }

    closedir(d);

    if (len + 4 > cap) {
        cap = len + 16;
        char *nb = (char *)realloc(buf, cap);
        if (!nb) {
            free(buf);
            return NULL;
        }
        buf = nb;
    }
    len += (size_t)snprintf(buf + len, cap - len, "]}");
    return buf;
}
