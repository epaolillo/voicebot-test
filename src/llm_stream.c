#include "llm_stream.h"
#include "config.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- simple dynamic string ---- */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} DynStr;

static void dynstr_init(DynStr *s, size_t initial)
{
    s->cap  = initial > 64 ? initial : 64;
    s->data = (char *)malloc(s->cap);
    s->len  = 0;
    s->data[0] = '\0';
}

static void dynstr_append(DynStr *s, const char *text, size_t n)
{
    if (s->len + n + 1 > s->cap) {
        s->cap = (s->len + n + 1) * 2;
        s->data = (char *)realloc(s->data, s->cap);
    }
    memcpy(s->data + s->len, text, n);
    s->len += n;
    s->data[s->len] = '\0';
}

static void dynstr_append_str(DynStr *s, const char *text)
{
    dynstr_append(s, text, strlen(text));
}

static void dynstr_clear(DynStr *s)
{
    s->len = 0;
    s->data[0] = '\0';
}

static void dynstr_free(DynStr *s)
{
    free(s->data);
    s->data = NULL;
    s->len = s->cap = 0;
}

/* ---- JSON helpers ---- */

static void json_escape(DynStr *out, const char *s)
{
    for (; *s; s++) {
        switch (*s) {
        case '"':  dynstr_append_str(out, "\\\""); break;
        case '\\': dynstr_append_str(out, "\\\\"); break;
        case '\n': dynstr_append_str(out, "\\n");  break;
        case '\r': dynstr_append_str(out, "\\r");  break;
        case '\t': dynstr_append_str(out, "\\t");  break;
        default:
            dynstr_append(out, s, 1);
        }
    }
}

/* curl write callback for non-streaming requests */
static size_t blocking_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    DynStr *buf = (DynStr *)userdata;
    size_t total = size * nmemb;
    dynstr_append(buf, (const char *)ptr, total);
    return total;
}

/* ---- conversation history with automatic summarization ---- */

#define MAX_HISTORY     50
#define SUMMARIZE_AT    40

typedef struct {
    char *role;
    char *content;
} Message;

static Message history[MAX_HISTORY];
static int history_count = 0;
static char api_key[256] = {0};
static char saved_system_prompt[2048] = {0};

static void history_add(const char *role, const char *content)
{
    if (history_count >= MAX_HISTORY) {
        free(history[1].role);
        free(history[1].content);
        memmove(&history[1], &history[2], sizeof(Message) * (MAX_HISTORY - 2));
        history_count = MAX_HISTORY - 1;
    }
    history[history_count].role = strdup(role);
    history[history_count].content = strdup(content);
    history_count++;
}

static const char *extract_json_content(const char *json, size_t *out_len)
{
    const char *key = "\"content\":\"";
    const char *p = strstr(json, key);
    if (!p)
        return NULL;

    p += strlen(key);
    const char *end = p;
    while (*end && !(*end == '"' && *(end - 1) != '\\'))
        end++;

    *out_len = (size_t)(end - p);
    return p;
}

static void summarize_history(void)
{
    if (history_count < SUMMARIZE_AT)
        return;

    int keep_recent = 6;
    int summarize_end = history_count - keep_recent;
    if (summarize_end <= 1)
        return;

    fprintf(stderr, "[llm] Summarizing %d messages into context...\n",
            summarize_end - 1);

    DynStr prompt;
    dynstr_init(&prompt, 4096);
    dynstr_append_str(&prompt,
        "Resumí brevemente la siguiente conversación en 2-3 oraciones. "
        "Incluí los temas tratados y cualquier dato importante mencionado. "
        "Respondé solo con el resumen, sin explicaciones:\n\n");

    for (int i = 1; i < summarize_end; i++) {
        dynstr_append_str(&prompt, history[i].role);
        dynstr_append_str(&prompt, ": ");
        dynstr_append_str(&prompt, history[i].content);
        dynstr_append_str(&prompt, "\n");
    }

    DynStr body;
    dynstr_init(&body, 2048);
    dynstr_append_str(&body, "{\"model\":\"" LLM_MODEL "\",\"stream\":false,");
    dynstr_append_str(&body, "\"max_tokens\":200,\"temperature\":0.3,");
    dynstr_append_str(&body, "\"messages\":[{\"role\":\"user\",\"content\":\"");
    json_escape(&body, prompt.data);
    dynstr_append_str(&body, "\"}]}");

    CURL *curl = curl_easy_init();
    if (!curl) {
        dynstr_free(&prompt);
        dynstr_free(&body);
        return;
    }

    char auth_header[300];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    DynStr response;
    dynstr_init(&response, 1024);

    curl_easy_setopt(curl, CURLOPT_URL, LLM_API_URL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, blocking_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    dynstr_free(&prompt);
    dynstr_free(&body);

    /* Free the old messages regardless of summary success */
    for (int i = 1; i < summarize_end; i++) {
        free(history[i].role);
        free(history[i].content);
    }
    int remaining = history_count - summarize_end;

    char *summary_text = NULL;

    if (res == CURLE_OK && http_code == 200 && response.len > 0) {
        size_t clen = 0;
        const char *c = extract_json_content(response.data, &clen);
        if (c && clen > 0) {
            summary_text = (char *)malloc(clen + 1);
            memcpy(summary_text, c, clen);
            summary_text[clen] = '\0';
        }
    }
    dynstr_free(&response);

    if (summary_text) {
        DynStr summary_msg;
        dynstr_init(&summary_msg, 512);
        dynstr_append_str(&summary_msg,
            "Resumen de la conversación anterior: ");
        dynstr_append_str(&summary_msg, summary_text);
        free(summary_text);

        /* Rebuild: system + summary + recent messages */
        Message new_hist[MAX_HISTORY];
        new_hist[0] = history[0];
        new_hist[1].role = strdup("system");
        new_hist[1].content = strdup(summary_msg.data);
        dynstr_free(&summary_msg);

        for (int i = 0; i < remaining; i++)
            new_hist[2 + i] = history[summarize_end + i];

        history_count = 2 + remaining;
        memcpy(history, new_hist, (size_t)history_count * sizeof(Message));
        fprintf(stderr, "[llm] History compressed: %d messages remain\n",
                history_count);
    } else {
        fprintf(stderr, "[llm] Summary failed, dropping old messages\n");
        memmove(&history[1], &history[summarize_end],
                (size_t)remaining * sizeof(Message));
        history_count = 1 + remaining;
    }
}

/* ---- request body builder ---- */

static void build_request_body(DynStr *body, const char *user_text)
{
    dynstr_clear(body);
    dynstr_append_str(body, "{\"model\":\"" LLM_MODEL "\",\"stream\":true,");
    dynstr_append_str(body, "\"max_tokens\":");

    char num[32];
    snprintf(num, sizeof(num), "%d", LLM_MAX_TOKENS);
    dynstr_append_str(body, num);

    snprintf(num, sizeof(num), "%.2f", (double)LLM_TEMPERATURE);
    dynstr_append_str(body, ",\"temperature\":");
    dynstr_append_str(body, num);

    dynstr_append_str(body, ",\"messages\":[");

    for (int i = 0; i < history_count; i++) {
        if (i > 0) dynstr_append_str(body, ",");
        dynstr_append_str(body, "{\"role\":\"");
        dynstr_append_str(body, history[i].role);
        dynstr_append_str(body, "\",\"content\":\"");
        json_escape(body, history[i].content);
        dynstr_append_str(body, "\"}");
    }

    if (history_count > 0)
        dynstr_append_str(body, ",");
    dynstr_append_str(body, "{\"role\":\"user\",\"content\":\"");
    json_escape(body, user_text);
    dynstr_append_str(body, "\"}]}");
}

/* ---- SSE stream parser ---- */

typedef struct {
    llm_sentence_cb cb;
    void           *userdata;
    DynStr          full_response;
    DynStr          line_buf;
} StreamCtx;

static void replace_single_question_marks(DynStr *s)
{
    DynStr out;
    dynstr_init(&out, s->len + 32);

    for (size_t i = 0; i < s->len; i++) {
        if (s->data[i] == '?') {
            size_t count = 0;
            while (i + count < s->len && s->data[i + count] == '?')
                count++;
            if (count == 1)
                dynstr_append_str(&out, "???");
            else
                dynstr_append(&out, &s->data[i], count);
            i += count - 1;
        } else {
            dynstr_append(&out, &s->data[i], 1);
        }
    }

    dynstr_clear(s);
    dynstr_append(s, out.data, out.len);
    dynstr_free(&out);
}

static void process_content_token(StreamCtx *ctx, const char *token, size_t len)
{
    dynstr_append(&ctx->full_response, token, len);
}

static void process_sse_line(StreamCtx *ctx, const char *line)
{
    if (strncmp(line, "data: ", 6) != 0)
        return;

    const char *data = line + 6;

    if (strcmp(data, "[DONE]") == 0)
        return;

    size_t content_len = 0;
    const char *content = extract_json_content(data, &content_len);
    if (content && content_len > 0) {
        DynStr unescaped;
        dynstr_init(&unescaped, content_len + 1);
        for (size_t i = 0; i < content_len; i++) {
            if (content[i] == '\\' && i + 1 < content_len) {
                switch (content[i + 1]) {
                case 'n':  dynstr_append(&unescaped, "\n", 1); i++; break;
                case 'r':  dynstr_append(&unescaped, "\r", 1); i++; break;
                case 't':  dynstr_append(&unescaped, "\t", 1); i++; break;
                case '"':  dynstr_append(&unescaped, "\"", 1); i++; break;
                case '\\': dynstr_append(&unescaped, "\\", 1); i++; break;
                default:   dynstr_append(&unescaped, &content[i], 1); break;
                }
            } else {
                dynstr_append(&unescaped, &content[i], 1);
            }
        }
        process_content_token(ctx, unescaped.data, unescaped.len);
        dynstr_free(&unescaped);
    }
}

static size_t stream_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    StreamCtx *ctx = (StreamCtx *)userdata;
    size_t total = size * nmemb;
    const char *chunk = (const char *)ptr;

    for (size_t i = 0; i < total; i++) {
        if (chunk[i] == '\n') {
            if (ctx->line_buf.len > 0) {
                process_sse_line(ctx, ctx->line_buf.data);
                dynstr_clear(&ctx->line_buf);
            }
        } else {
            dynstr_append(&ctx->line_buf, &chunk[i], 1);
        }
    }

    return total;
}

/* ---- public API ---- */

int llm_stream_init(const char *key, const char *system_prompt)
{
    if (!key || key[0] == '\0') {
        fprintf(stderr, "[llm] No API key provided\n");
        return -1;
    }
    snprintf(api_key, sizeof(api_key), "%s", key);
    snprintf(saved_system_prompt, sizeof(saved_system_prompt), "%s", system_prompt);

    history_add("system", saved_system_prompt);

    fprintf(stderr, "[llm] Initialized (model=%s, history=%d/%d)\n",
            LLM_MODEL, MAX_HISTORY, SUMMARIZE_AT);
    return 0;
}

int llm_stream_chat(const char *user_text, llm_sentence_cb sentence_cb, void *userdata)
{
    if (!user_text || user_text[0] == '\0')
        return -1;

    summarize_history();

    DynStr body;
    dynstr_init(&body, 2048);
    build_request_body(&body, user_text);

    CURL *curl = curl_easy_init();
    if (!curl) {
        dynstr_free(&body);
        return -1;
    }

    char auth_header[300];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    StreamCtx ctx;
    ctx.cb = sentence_cb;
    ctx.userdata = userdata;
    dynstr_init(&ctx.full_response, 1024);
    dynstr_init(&ctx.line_buf, 512);

    curl_easy_setopt(curl, CURLOPT_URL, LLM_API_URL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    struct timespec llm_t0;
    clock_gettime(CLOCK_MONOTONIC, &llm_t0);

    CURLcode res = curl_easy_perform(curl);

    struct timespec llm_t1;
    clock_gettime(CLOCK_MONOTONIC, &llm_t1);
    long llm_ms = (llm_t1.tv_sec - llm_t0.tv_sec) * 1000
                + (llm_t1.tv_nsec - llm_t0.tv_nsec) / 1000000;
    fprintf(stderr, "\033[1;31m  [TIMER] LLM streaming: %ldms\033[0m\n", llm_ms);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "[llm] curl error: %s\n", curl_easy_strerror(res));
        dynstr_free(&body);
        dynstr_free(&ctx.full_response);
        dynstr_free(&ctx.line_buf);
        return -1;
    }

    if (http_code != 200) {
        fprintf(stderr, "[llm] HTTP %ld\n", http_code);
        dynstr_free(&body);
        dynstr_free(&ctx.full_response);
        dynstr_free(&ctx.line_buf);
        return -1;
    }

    history_add("user", user_text);
    if (ctx.full_response.len > 0) {
        replace_single_question_marks(&ctx.full_response);

        char *start = ctx.full_response.data;
        while (*start == ' ') start++;

        if (*start != '\0' && sentence_cb)
            sentence_cb(start, userdata);

        history_add("assistant", ctx.full_response.data);
    }

    fprintf(stderr, "[llm] History: %d/%d messages\n", history_count, MAX_HISTORY);

    dynstr_free(&body);
    dynstr_free(&ctx.full_response);
    dynstr_free(&ctx.line_buf);
    return 0;
}

void llm_stream_add_message(const char *role, const char *content)
{
    if (!role || !content)
        return;
    history_add(role, content);
}

void llm_stream_clear_history(void)
{
    for (int i = 0; i < history_count; i++) {
        free(history[i].role);
        free(history[i].content);
        history[i].role = NULL;
        history[i].content = NULL;
    }
    history_count = 0;
    history_add("system", saved_system_prompt);
    fprintf(stderr, "[llm] History cleared\n");
}

void llm_stream_free(void)
{
    for (int i = 0; i < history_count; i++) {
        free(history[i].role);
        free(history[i].content);
    }
    history_count = 0;
    fprintf(stderr, "[llm] Freed\n");
}
