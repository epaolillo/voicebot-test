#include "tts_api_server.h"

#include <microhttpd.h>

#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

extern "C" {
#include "config.h"
#include "tts_models.h"
#include "tts_playback.h"
#include "tts_synthesize.h"
#include "wav_utils.h"
}

namespace {

struct ServerCtx {
    const char *project_dir;
    const char *api_key;
};

struct ConnData {
    std::string        body;
    unsigned long long expect_body = 0;
    bool               answered = false;
};

static struct MHD_Daemon *g_daemon = nullptr;
static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
    struct MHD_Daemon *d = g_daemon;
    if (d) {
        MHD_stop_daemon(d);
        g_daemon = nullptr;
    }
}

static void add_cors(struct MHD_Response *r)
{
    MHD_add_response_header(r, "Access-Control-Allow-Origin", "*");
    MHD_add_response_header(r, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    MHD_add_response_header(r, "Access-Control-Allow-Headers", "Content-Type, Authorization");
}

static bool auth_ok(struct MHD_Connection *conn, const char *secret)
{
    if (!secret || !secret[0])
        return true;
    const char *h = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Authorization");
    if (!h)
        return false;
    if (strncasecmp(h, "Bearer ", 7) != 0)
        return false;
    const char *tok = h + 7;
    while (*tok == ' ')
        tok++;
    return strcmp(tok, secret) == 0;
}

static enum MHD_Result queue_json(struct MHD_Connection *conn, unsigned int status, const char *json)
{
    size_t n = strlen(json);
    char *buf = (char *)malloc(n + 1);
    if (!buf)
        return MHD_NO;
    memcpy(buf, json, n + 1);
    struct MHD_Response *r = MHD_create_response_from_buffer(n, buf, MHD_RESPMEM_MUST_FREE);
    if (!r) {
        free(buf);
        return MHD_NO;
    }
    MHD_add_response_header(r, "Content-Type", "application/json; charset=utf-8");
    add_cors(r);
    enum MHD_Result ret = MHD_queue_response(conn, status, r);
    MHD_destroy_response(r);
    return ret;
}

static enum MHD_Result queue_empty(struct MHD_Connection *conn, unsigned int status)
{
    static char z;
    struct MHD_Response *r = MHD_create_response_from_buffer(0, &z, MHD_RESPMEM_PERSISTENT);
    if (!r)
        return MHD_NO;
    add_cors(r);
    enum MHD_Result ret = MHD_queue_response(conn, status, r);
    MHD_destroy_response(r);
    return ret;
}

static bool extract_json_string(const std::string &body, const char *key, std::string &out)
{
    /* Allow optional whitespace before ':' (pretty-printed JSON). */
    std::string needle = std::string("\"") + key + "\"";
    size_t p = body.find(needle);
    if (p == std::string::npos)
        return false;
    p += needle.size();
    while (p < body.size() && std::isspace((unsigned char)body[p]))
        p++;
    if (p >= body.size() || body[p] != ':')
        return false;
    p++;
    while (p < body.size() && std::isspace((unsigned char)body[p]))
        p++;
    if (p >= body.size() || body[p] != '"')
        return false;
    p++;
    out.clear();
    while (p < body.size()) {
        char c = body[p++];
        if (c == '"')
            break;
        if (c == '\\' && p < body.size())
            out += body[p++];
        else
            out += c;
    }
    return true;
}

static bool extract_json_bool(const std::string &body, const char *key, bool *out)
{
    std::string needle = std::string("\"") + key + "\"";
    size_t p = body.find(needle);
    if (p == std::string::npos)
        return false;
    p += needle.size();
    while (p < body.size() && std::isspace((unsigned char)body[p]))
        p++;
    if (p >= body.size() || body[p] != ':')
        return false;
    p++;
    while (p < body.size() && std::isspace((unsigned char)body[p]))
        p++;
    if (p + 4 <= body.size() && body.compare(p, 4, "true") == 0) {
        *out = true;
        return true;
    }
    if (p + 5 <= body.size() && body.compare(p, 5, "false") == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool extract_json_speed(const std::string &body, double *out)
{
    std::string needle = "\"speed\"";
    size_t p = body.find(needle);
    if (p == std::string::npos)
        return false;
    p += needle.size();
    while (p < body.size() && std::isspace((unsigned char)body[p]))
        p++;
    if (p >= body.size() || body[p] != ':')
        return false;
    p++;
    while (p < body.size() && std::isspace((unsigned char)body[p]))
        p++;
    char *end = nullptr;
    double v = strtod(body.c_str() + p, &end);
    if (end == body.c_str() + p)
        return false;
    *out = v;
    return true;
}

static bool is_openai_tts_model(const char *m)
{
    if (!m)
        return false;
    return strcmp(m, "tts-1") == 0 || strcmp(m, "tts-1-hd") == 0 ||
           strcmp(m, "gpt-4o-mini-tts") == 0 || strcmp(m, "gpt-4o-mini-tts-2025-12-15") == 0;
}

static void json_trim_string(std::string &s)
{
    size_t a = 0;
    while (a < s.size() && std::isspace((unsigned char)s[a]))
        a++;
    size_t b = s.size();
    while (b > a && std::isspace((unsigned char)s[b - 1]))
        b--;
    if (a > 0 || b < s.size())
        s = s.substr(a, b - a);
}

/* Map client variants to "pcm" or "wav". Returns empty if unknown (caller may default to pcm). */
static std::string canonical_response_format(std::string rf, bool *recognized)
{
    *recognized = true;
    json_trim_string(rf);
    if (rf.empty())
        return "pcm";

    std::string lc = rf;
    std::transform(lc.begin(), lc.end(), lc.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });

    if (lc == "raw" || lc == "pcm" || lc == "s16le" || lc == "audio/pcm" || lc == "audio/l16" ||
        lc == "linear16" || lc == "linear16le")
        return "pcm";
    if (lc == "wav" || lc == "wave" || lc == "audio/wav" || lc == "audio/x-wav" || lc == "audio/wave")
        return "wav";

    *recognized = false;
    return rf;
}

static float speed_to_length_scale(double speed)
{
    if (speed < 0.25)
        speed = 0.25;
    if (speed > 4.0)
        speed = 4.0;
    double ls = (double)PIPER_LENGTH_SCALE / speed;
    if (ls < 0.25)
        ls = 0.25;
    if (ls > 2.5)
        ls = 2.5;
    return (float)ls;
}

static const char *b64 =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const uint8_t *data, size_t len)
{
    std::string out;
    out.reserve(4 * ((len + 2) / 3));
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16;
        if (i + 1 < len)
            v |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len)
            v |= (uint32_t)data[i + 2];
        out += b64[(v >> 18) & 63];
        out += b64[(v >> 12) & 63];
        if (i + 1 < len)
            out += b64[(v >> 6) & 63];
        else
            out += '=';
        if (i + 2 < len)
            out += b64[v & 63];
        else
            out += '=';
    }
    return out;
}

struct PcmStreamCls {
    int     fd  = -1;
    pid_t   pid = 0;
};

static ssize_t pcm_reader_callback(void *cls, uint64_t pos, char *buf, size_t max)
{
    (void)pos;
    auto *s = static_cast<PcmStreamCls *>(cls);
    if (s->fd < 0)
        return MHD_CONTENT_READER_END_OF_STREAM;

    ssize_t n;
    for (;;) {
        n = read(s->fd, buf, max);
        if (n >= 0 || errno != EINTR)
            break;
    }
    if (n < 0)
        return MHD_CONTENT_READER_END_WITH_ERROR;
    if (n == 0) {
        close(s->fd);
        s->fd = -1;
        if (s->pid > 0) {
            waitpid(s->pid, nullptr, 0);
            s->pid = 0;
        }
        return MHD_CONTENT_READER_END_OF_STREAM;
    }
    return n;
}

static void pcm_reader_free(void *cls)
{
    auto *s = static_cast<PcmStreamCls *>(cls);
    if (s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
    }
    if (s->pid > 0) {
        kill(s->pid, SIGKILL);
        waitpid(s->pid, nullptr, 0);
        s->pid = 0;
    }
    delete s;
}

struct SseStreamCls {
    int                  fd = -1;
    pid_t                pid = 0;
    std::string          line;
    size_t               line_off = 0;
    std::vector<uint8_t> binbuf;
};

static ssize_t sse_reader_callback(void *cls, uint64_t pos, char *buf, size_t max)
{
    (void)pos;
    auto *s = static_cast<SseStreamCls *>(cls);

    if (s->line_off < s->line.size()) {
        size_t left = s->line.size() - s->line_off;
        size_t cpy  = std::min(left, max);
        memcpy(buf, s->line.c_str() + s->line_off, cpy);
        s->line_off += cpy;
        if (s->line_off >= s->line.size()) {
            s->line.clear();
            s->line_off = 0;
        }
        return (ssize_t)cpy;
    }

    if (s->fd < 0)
        return MHD_CONTENT_READER_END_OF_STREAM;

    s->binbuf.resize(2400);
    ssize_t n;
    for (;;) {
        n = read(s->fd, s->binbuf.data(), s->binbuf.size());
        if (n >= 0 || errno != EINTR)
            break;
    }
    if (n < 0)
        return MHD_CONTENT_READER_END_WITH_ERROR;
    if (n == 0) {
        close(s->fd);
        s->fd = -1;
        if (s->pid > 0) {
            waitpid(s->pid, nullptr, 0);
            s->pid = 0;
        }
        s->line     = "data: [DONE]\n\n";
        s->line_off = 0;
        if (!s->line.empty() && max > 0) {
            size_t cpy = std::min(s->line.size(), (size_t)max);
            memcpy(buf, s->line.c_str(), cpy);
            s->line_off = cpy;
            if (s->line_off >= s->line.size()) {
                s->line.clear();
                s->line_off = 0;
            }
            return (ssize_t)cpy;
        }
        return MHD_CONTENT_READER_END_OF_STREAM;
    }

    s->line   = "data: " + base64_encode(s->binbuf.data(), (size_t)n) + "\n\n";
    s->line_off = 0;
    size_t cpy  = std::min(s->line.size(), (size_t)max);
    memcpy(buf, s->line.c_str(), cpy);
    s->line_off = cpy;
    if (s->line_off >= s->line.size()) {
        s->line.clear();
        s->line_off = 0;
    }
    return (ssize_t)cpy;
}

static void sse_reader_free(void *cls)
{
    auto *s = static_cast<SseStreamCls *>(cls);
    if (s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
    }
    if (s->pid > 0) {
        kill(s->pid, SIGKILL);
        waitpid(s->pid, nullptr, 0);
        s->pid = 0;
    }
    delete s;
}

static enum MHD_Result handle_speech(struct MHD_Connection *conn, ServerCtx *srv, const std::string &body)
{
    std::string input, model_field, voice_field, response_format, stream_format;
    if (!extract_json_string(body, "input", input) || input.empty())
        return queue_json(conn, 400, "{\"error\":{\"message\":\"missing or empty input\"}}");

    extract_json_string(body, "model", model_field);
    extract_json_string(body, "voice", voice_field);
    extract_json_string(body, "response_format", response_format);
    extract_json_string(body, "stream_format", stream_format);

    if (response_format.empty()) {
        std::string format_alias;
        if (extract_json_string(body, "format", format_alias))
            response_format = std::move(format_alias);
    }

    bool stream = false;
    if (!extract_json_bool(body, "stream", &stream))
        stream = false;

    double speed = 1.0;
    if (!extract_json_speed(body, &speed))
        speed = 1.0;
    float length_scale = speed_to_length_scale(speed);

    if (response_format.empty())
        response_format = "pcm";

    /* OpenAI clients default to mp3/opus; we only stream raw PCM. Coerce for streaming requests. */
    if (stream) {
        std::string probe = response_format;
        json_trim_string(probe);
        std::transform(probe.begin(), probe.end(), probe.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        if (probe == "mp3" || probe == "opus" || probe == "aac" || probe == "flac" || probe == "mpeg")
            response_format = "pcm";
    }

    bool fmt_recognized = true;
    response_format     = canonical_response_format(std::move(response_format), &fmt_recognized);
    if (!fmt_recognized) {
        char safe[80];
        size_t j = 0;
        for (size_t i = 0; i < response_format.size() && j + 1 < sizeof(safe); i++) {
            unsigned char c = (unsigned char)response_format[i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                c == '-' || c == '/' || c == '.')
                safe[j++] = (char)c;
            else
                safe[j++] = '_';
        }
        safe[j] = '\0';
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "{\"error\":{\"message\":\"unsupported response_format \\\"%s\\\" "
                 "(supported: pcm, wav, raw, audio/pcm, audio/wav; not mp3/opus)\"}}",
                 safe);
        return queue_json(conn, 400, msg);
    }

    const char *piper_bin   = tts_get_piper_bin();
    const char *piper_ld    = tts_get_piper_dir();
    const char *espeak      = tts_get_espeak_data();

    char onnx_path[2048];
    char default_id[256];

    auto try_voice = [&](const std::string &id) -> bool {
        if (id.empty())
            return false;
        return tts_models_resolve_voice_path(srv->project_dir, id.c_str(), onnx_path,
                                             sizeof(onnx_path)) == 0;
    };

    std::string chosen;
    if (try_voice(voice_field))
        chosen = voice_field;
    else if (try_voice(model_field))
        chosen = model_field;
    else if (is_openai_tts_model(model_field.c_str()) || model_field.empty()) {
        if (tts_models_default_voice_id(srv->project_dir, default_id, sizeof(default_id)) != 0)
            return queue_json(conn, 500, "{\"error\":{\"message\":\"no voice models found\"}}");
        chosen = default_id;
        if (!try_voice(chosen))
            return queue_json(conn, 500, "{\"error\":{\"message\":\"default voice resolve failed\"}}");
    } else
        return queue_json(conn, 400, "{\"error\":{\"message\":\"unknown model or voice\"}}");

    bool use_sse         = stream && stream_format == "sse";
    bool use_audio_stream = stream && (stream_format.empty() || stream_format == "audio");

    if (stream && !use_sse && !use_audio_stream)
        return queue_json(conn, 400, "{\"error\":{\"message\":\"unsupported stream_format\"}}");

    if (stream && response_format == "wav")
        return queue_json(conn, 400,
                          "{\"error\":{\"message\":\"streaming emits s16le PCM only; use "
                          "response_format \\\"pcm\\\" or \\\"raw\\\" and play with aplay -t raw -f S16_LE -r <Hz> -c 1 "
                          "(Hz from X-Sample-Rate), or stream false for a single WAV file\"}}");

    int sample_rate =
        tts_models_read_sample_rate(onnx_path, tts_get_sample_rate());

    if (!stream && response_format == "wav") {
        uint8_t *pcm = nullptr;
        size_t   pcm_size = 0;
        if (tts_synthesize_pcm_collect(input.c_str(), onnx_path, length_scale, piper_bin, piper_ld,
                                       espeak, &pcm, &pcm_size) != 0)
            return queue_json(conn, 500, "{\"error\":{\"message\":\"synthesis failed\"}}");

        const int16_t *samples = reinterpret_cast<const int16_t *>(pcm);
        int sample_count = (int)(pcm_size / 2);
        uint8_t *wav = nullptr;
        size_t wav_size = 0;
        if (wav_build(samples, sample_count, sample_rate, 1, &wav, &wav_size) != 0) {
            free(pcm);
            return queue_json(conn, 500, "{\"error\":{\"message\":\"wav build failed\"}}");
        }
        free(pcm);

        struct MHD_Response *r =
            MHD_create_response_from_buffer(wav_size, wav, MHD_RESPMEM_MUST_FREE);
        if (!r) {
            free(wav);
            return MHD_NO;
        }
        MHD_add_response_header(r, "Content-Type", "audio/wav");
        add_cors(r);
        enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_OK, r);
        MHD_destroy_response(r);
        return ret;
    }

    if (!stream && response_format == "pcm") {
        uint8_t *pcm = nullptr;
        size_t   pcm_size = 0;
        if (tts_synthesize_pcm_collect(input.c_str(), onnx_path, length_scale, piper_bin, piper_ld,
                                       espeak, &pcm, &pcm_size) != 0)
            return queue_json(conn, 500, "{\"error\":{\"message\":\"synthesis failed\"}}");

        struct MHD_Response *r =
            MHD_create_response_from_buffer(pcm_size, pcm, MHD_RESPMEM_MUST_FREE);
        if (!r) {
            free(pcm);
            return MHD_NO;
        }
        char sr_hdr[32];
        snprintf(sr_hdr, sizeof(sr_hdr), "%d", sample_rate);
        MHD_add_response_header(r, "Content-Type", "audio/pcm");
        MHD_add_response_header(r, "X-Sample-Rate", sr_hdr);
        add_cors(r);
        enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_OK, r);
        MHD_destroy_response(r);
        return ret;
    }

    /* streaming */
    if (use_sse) {
        auto *sse = new SseStreamCls();
        if (tts_synthesize_pcm_stream_start(input.c_str(), onnx_path, length_scale, piper_bin,
                                            piper_ld, espeak, &sse->fd, &sse->pid) != 0) {
            delete sse;
            return queue_json(conn, 500, "{\"error\":{\"message\":\"synthesis start failed\"}}");
        }

        struct MHD_Response *r = MHD_create_response_from_callback(
            MHD_SIZE_UNKNOWN, 4096, sse_reader_callback, sse, sse_reader_free);
        if (!r) {
            sse_reader_free(sse);
            return MHD_NO;
        }
        MHD_add_response_header(r, "Content-Type", "text/event-stream; charset=utf-8");
        MHD_add_response_header(r, "Cache-Control", "no-cache");
        add_cors(r);
        enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_OK, r);
        MHD_destroy_response(r);
        return ret;
    }

    if (use_audio_stream) {
        auto *pcm = new PcmStreamCls();
        if (tts_synthesize_pcm_stream_start(input.c_str(), onnx_path, length_scale, piper_bin,
                                            piper_ld, espeak, &pcm->fd, &pcm->pid) != 0) {
            delete pcm;
            return queue_json(conn, 500, "{\"error\":{\"message\":\"synthesis start failed\"}}");
        }

        struct MHD_Response *r = MHD_create_response_from_callback(
            MHD_SIZE_UNKNOWN, 8192, pcm_reader_callback, pcm, pcm_reader_free);
        if (!r) {
            pcm_reader_free(pcm);
            return MHD_NO;
        }
        char sr_hdr[32];
        snprintf(sr_hdr, sizeof(sr_hdr), "%d", sample_rate);
        MHD_add_response_header(r, "Content-Type", "audio/pcm");
        MHD_add_response_header(r, "X-Sample-Rate", sr_hdr);
        add_cors(r);
        enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_OK, r);
        MHD_destroy_response(r);
        return ret;
    }

    return queue_json(conn, 400, "{\"error\":{\"message\":\"invalid stream options\"}}");
}

static void request_completed(void *cls, struct MHD_Connection *connection, void **con_cls,
                             enum MHD_RequestTerminationCode toe)
{
    (void)cls;
    (void)connection;
    (void)toe;
    ConnData *cd = static_cast<ConnData *>(*con_cls);
    if (cd) {
        delete cd;
        *con_cls = nullptr;
    }
}

static enum MHD_Result handler(void *cls, struct MHD_Connection *connection, const char *url,
                               const char *method, const char *version, const char *upload_data,
                               size_t *upload_data_size, void **con_cls)
{
    (void)version;
    auto *srv = static_cast<ServerCtx *>(cls);

    if (!auth_ok(connection, srv->api_key))
        return queue_json(connection, 401, "{\"error\":{\"message\":\"unauthorized\"}}");

    ConnData *cd = static_cast<ConnData *>(*con_cls);
    if (!cd) {
        if (strcmp(method, "POST") == 0) {
            const char *cl =
                MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Content-Length");
            if (!cl)
                return queue_json(connection, 411,
                                  "{\"error\":{\"message\":\"Content-Length required\"}}");
            unsigned long long ex = strtoull(cl, nullptr, 10);
            if (ex > 512ull * 1024ull)
                return queue_json(connection, 413, "{\"error\":{\"message\":\"body too large\"}}");
            cd             = new ConnData();
            cd->expect_body = ex;
            *con_cls       = cd;
        } else {
            cd             = new ConnData();
            cd->expect_body = 0;
            *con_cls       = cd;
        }
    }

    if (*upload_data_size > 0) {
        cd->body.append(upload_data, *upload_data_size);
        *upload_data_size = 0;
        return MHD_YES;
    }

    if (strcmp(method, "POST") == 0 && cd->body.size() < cd->expect_body)
        return MHD_YES;

    if (cd->answered)
        return MHD_YES;
    cd->answered = true;

    if (strcmp(method, "OPTIONS") == 0)
        return queue_empty(connection, 204);

    /* OpenAI uses /v1/...; accept bare paths when base_url omits /v1 (common misconfiguration). */
    if ((strcmp(url, "/v1/models") == 0 || strcmp(url, "/models") == 0) && strcmp(method, "GET") == 0) {
        char *json = tts_models_build_json_list(srv->project_dir);
        if (!json)
            return queue_json(connection, 500, "{\"error\":{\"message\":\"models list failed\"}}");
        size_t n = strlen(json);
        struct MHD_Response *r = MHD_create_response_from_buffer(n, json, MHD_RESPMEM_MUST_FREE);
        if (!r) {
            free(json);
            return MHD_NO;
        }
        MHD_add_response_header(r, "Content-Type", "application/json; charset=utf-8");
        add_cors(r);
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, r);
        MHD_destroy_response(r);
        return ret;
    }

    if ((strcmp(url, "/v1/audio/speech") == 0 || strcmp(url, "/audio/speech") == 0) &&
        strcmp(method, "POST") == 0)
        return handle_speech(connection, srv, cd->body);

    return queue_json(connection, 404, "{\"error\":{\"message\":\"not found\"}}");
}

} /* namespace */

extern "C" int run_tts_api_server(const char *project_dir, int port, const char *api_key)
{
    if (!project_dir || port <= 0 || port > 65535)
        return -1;

    if (tts_auto_discover(project_dir) < 0)
        return -1;

    ServerCtx ctx;
    ctx.project_dir = project_dir;
    ctx.api_key     = api_key;

    g_stop   = 0;
    g_daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_ERROR_LOG, (uint16_t)port,
                                nullptr, nullptr, &handler, &ctx, MHD_OPTION_NOTIFY_COMPLETED,
                                request_completed, nullptr, MHD_OPTION_END);
    if (!g_daemon)
        return -1;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    fprintf(stderr, "[api] OpenAI-compatible TTS listening on port %d\n", port);
    fprintf(stderr, "[api] GET /v1/models (or /models)  POST /v1/audio/speech (or /audio/speech)\n");

    while (!g_stop)
        pause();

    return 0;
}
