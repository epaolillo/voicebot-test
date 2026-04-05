#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cstdint>
#include <vector>
#include <climits>
#include <unistd.h>
#include <libgen.h>
#include <time.h>
#include <sys/stat.h>
#include <pthread.h>

extern "C" {
#include "audio_capture.h"
#include "noise_reduce.h"
#include "vad.h"
#include "wav_utils.h"
#include "whisper_api.h"
#include "whisper_local.h"
#include "llm_stream.h"
#include "tts_playback.h"
#include "fillers.h"
#include "config.h"
}

static volatile sig_atomic_t running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

/* ---- Pre-buffer ---- */

#define PREBUF_FRAMES 30

struct PreBuffer {
    int16_t data[PREBUF_FRAMES][RNNOISE_FRAME_SIZE];
    int     head;
    int     count;
};

static void prebuf_init(PreBuffer *pb)
{
    pb->head = 0;
    pb->count = 0;
}

static void prebuf_push(PreBuffer *pb, const int16_t *frame)
{
    memcpy(pb->data[pb->head], frame, RNNOISE_FRAME_SIZE * sizeof(int16_t));
    pb->head = (pb->head + 1) % PREBUF_FRAMES;
    if (pb->count < PREBUF_FRAMES)
        pb->count++;
}

static void prebuf_drain(PreBuffer *pb, std::vector<int16_t> &out)
{
    if (pb->count == 0)
        return;

    int start = (pb->count < PREBUF_FRAMES) ? 0 : pb->head;

    for (int i = 0; i < pb->count; i++) {
        int idx = (start + i) % PREBUF_FRAMES;
        out.insert(out.end(), pb->data[idx], pb->data[idx] + RNNOISE_FRAME_SIZE);
    }

    pb->count = 0;
    pb->head = 0;
}

/* ---- Downsample ---- */

static void downsample_filtered(const int16_t *src, int src_count,
                                 int16_t *dst, int *dst_count)
{
    static const float coeffs[7] = {
        0.05f, 0.10f, 0.20f, 0.30f, 0.20f, 0.10f, 0.05f
    };
    int half = 3;
    int out = 0;

    for (int i = 0; i + 2 < src_count; i += DOWNSAMPLE_RATIO) {
        float sum = 0.0f;
        for (int k = -half; k <= half; k++) {
            int idx = i + k;
            float sample = 0.0f;
            if (idx >= 0 && idx < src_count)
                sample = (float)src[idx];
            sum += sample * coeffs[k + half];
        }
        if (sum > 32767.0f) sum = 32767.0f;
        if (sum < -32768.0f) sum = -32768.0f;
        dst[out++] = (int16_t)sum;
    }
    *dst_count = out;
}

/* ---- Timing ---- */

#define RED    "\033[1;31m"
#define RESET  "\033[0m"

static long elapsed_ms(struct timespec *start)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long sec  = now.tv_sec - start->tv_sec;
    long nsec = now.tv_nsec - start->tv_nsec;
    return sec * 1000 + nsec / 1000000;
}

static void timer_start(struct timespec *t)
{
    clock_gettime(CLOCK_MONOTONIC, t);
}

static long timer_stop(struct timespec *t, const char *label)
{
    long ms = elapsed_ms(t);
    fprintf(stderr, RED "  [TIMER] %s: %ldms" RESET "\n", label, ms);
    return ms;
}

/* ---- Callbacks ---- */

static void on_sentence(const char *sentence, void *userdata)
{
    int autolistening = userdata ? *(int *)userdata : 0;
    fprintf(stdout, "  [BOT] %s\n", sentence);
    fflush(stdout);

    struct timespec t;
    timer_start(&t);
    tts_speak(sentence);
    timer_stop(&t, "TTS");

    if (!autolistening)
        audio_capture_flush();
}

static void resolve_project_dir(char *out, size_t out_size)
{
    char cwd[2048];
    if (!getcwd(cwd, sizeof(cwd))) {
        snprintf(out, out_size, ".");
        return;
    }

    char test[2200];
    struct stat st;

    snprintf(test, sizeof(test), "%s/piper", cwd);
    if (stat(test, &st) == 0) {
        snprintf(out, out_size, "%s", cwd);
        return;
    }

    snprintf(test, sizeof(test), "%s/../piper", cwd);
    if (stat(test, &st) == 0) {
        snprintf(out, out_size, "%s/..", cwd);
        return;
    }

    snprintf(out, out_size, "%s", cwd);
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s --openai <key> --company <name> --name <first> --lastname <last>\n"
        "\n"
        "Required:\n"
        "  --openai <key>       OpenAI API key (always needed for LLM)\n"
        "  --company <name>     Company name for greeting\n"
        "  --name <first>       Caller's first name\n"
        "  --lastname <last>    Caller's last name\n"
        "\n"
        "Optional:\n"
        "  --whisper-local [path]  Use local whisper.cpp STT (default: %s)\n"
        "  --fillers              Enable thinking/backchannel fillers\n"
        "  --autolistening        Keep mic active during TTS playback\n"
        "  --help                 Show this help\n",
        prog, WHISPER_LOCAL_MODEL);
}

/* ---- Main ---- */

int main(int argc, char *argv[])
{
    const char *openai_key    = nullptr;
    const char *company       = nullptr;
    const char *name          = nullptr;
    const char *lastname      = nullptr;
    const char *whisper_model = nullptr;
    int use_whisper_local     = 0;
    int use_fillers           = 0;
    int autolistening         = DEFAULT_AUTOLISTENING;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--openai") == 0 && i + 1 < argc) {
            openai_key = argv[++i];
        } else if (strcmp(argv[i], "--company") == 0 && i + 1 < argc) {
            company = argv[++i];
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            name = argv[++i];
        } else if (strcmp(argv[i], "--lastname") == 0 && i + 1 < argc) {
            lastname = argv[++i];
        } else if (strcmp(argv[i], "--whisper-local") == 0) {
            use_whisper_local = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                whisper_model = argv[++i];
        } else if (strcmp(argv[i], "--fillers") == 0) {
            use_fillers = 1;
        } else if (strcmp(argv[i], "--autolistening") == 0) {
            autolistening = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!openai_key || !company || !name || !lastname) {
        fprintf(stderr, "Error: --openai, --company, --name, and --lastname are required.\n\n");
        print_usage(argv[0]);
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    fillers_init();

    char project_dir[PATH_MAX];
    resolve_project_dir(project_dir, sizeof(project_dir));

    fprintf(stderr, "=== Piper Voicebot ===\n");
    fprintf(stderr, "[config] company=%s  caller=%s %s  autolistening=%d  stt=%s  fillers=%s\n",
            company, name, lastname, autolistening,
            use_whisper_local ? "local" : "api",
            use_fillers ? "on" : "off");

    if (tts_auto_discover(project_dir) < 0) {
        fprintf(stderr, "\nPiper TTS not found. Run:\n  bash setup.sh\n");
        return 1;
    }

    if (audio_capture_open() < 0) {
        fprintf(stderr, "Failed to open audio capture\n");
        return 1;
    }

    if (noise_reduce_init() < 0) {
        fprintf(stderr, "Failed to init noise reduction\n");
        audio_capture_close();
        return 1;
    }

    if (use_whisper_local) {
        char model_path[PATH_MAX + 256];
        if (whisper_model) {
            snprintf(model_path, sizeof(model_path), "%s", whisper_model);
        } else {
            snprintf(model_path, sizeof(model_path), "%s/%s",
                     project_dir, WHISPER_LOCAL_MODEL);
        }
        if (whisper_local_init(model_path) < 0) {
            fprintf(stderr, "Failed to init local Whisper. Run:\n  bash setup.sh\n");
            noise_reduce_free();
            audio_capture_close();
            return 1;
        }
    } else {
        if (whisper_api_init(openai_key) < 0) {
            fprintf(stderr, "Failed to init Whisper API\n");
            noise_reduce_free();
            audio_capture_close();
            return 1;
        }
    }

    char system_prompt[2048];
    snprintf(system_prompt, sizeof(system_prompt),
             LLM_SYSTEM_PROMPT_FMT, name, lastname, company);

    if (llm_stream_init(openai_key, system_prompt) < 0) {
        fprintf(stderr, "Failed to init LLM\n");
        whisper_api_free();
        noise_reduce_free();
        audio_capture_close();
        return 1;
    }

    vad_reset();

    char greeting[512];
    snprintf(greeting, sizeof(greeting),
             "Buenos días, hablo con %s %s%s Te llamo de %s.",
             name, lastname, "???", company);

    fprintf(stderr, "\nWaiting for caller... (Ctrl+C to quit)\n\n");

    int16_t frame_raw[RNNOISE_FRAME_SIZE];
    int16_t frame_denoised[RNNOISE_FRAME_SIZE];

    PreBuffer prebuf;
    prebuf_init(&prebuf);

    std::vector<int16_t> raw_recording;
    raw_recording.reserve(CAPTURE_SAMPLE_RATE * 30);

    int speech_active = 0;
    int first_turn = 1;

    struct timespec speech_start_time;
    int next_backchannel_ms = fillers_backchannel_interval_ms();
    int backchannel_armed = 0;

    while (running) {
        int n = audio_capture_read(frame_raw, RNNOISE_FRAME_SIZE);
        if (n <= 0) {
            if (!running) break;
            continue;
        }

        if (!autolistening && tts_is_speaking()) {
            if (speech_active) {
                speech_active = 0;
                backchannel_armed = 0;
                raw_recording.clear();
                prebuf_init(&prebuf);
                vad_reset();
            }
            continue;
        }

        memcpy(frame_denoised, frame_raw, (size_t)n * sizeof(int16_t));
        float rnn_vad = noise_reduce_process(frame_denoised, n);

        VadState state = vad_process(frame_denoised, n, rnn_vad);

        if (!speech_active)
            prebuf_push(&prebuf, frame_raw);

        if (state == VAD_SPEECH && !speech_active) {
            speech_active = 1;
            backchannel_armed = 1;
            clock_gettime(CLOCK_MONOTONIC, &speech_start_time);
            next_backchannel_ms = fillers_backchannel_interval_ms();

            prebuf_drain(&prebuf, raw_recording);
            raw_recording.insert(raw_recording.end(), frame_raw, frame_raw + n);

        } else if (speech_active && (state == VAD_SPEECH || state == VAD_SPEECH_END)) {
            raw_recording.insert(raw_recording.end(), frame_raw, frame_raw + n);
        }

        if (use_fillers && speech_active && backchannel_armed && state == VAD_SPEECH) {
            long ms = elapsed_ms(&speech_start_time);
            if (ms >= next_backchannel_ms) {
                const char *cue = fillers_backchannel();
                fprintf(stderr, "  [~] %s\n", cue);
                tts_speak(cue);
                llm_stream_add_message("assistant", cue);
                if (!autolistening)
                    audio_capture_flush();

                clock_gettime(CLOCK_MONOTONIC, &speech_start_time);
                next_backchannel_ms = fillers_backchannel_interval_ms();
            }
        }

        if (state == VAD_SPEECH_END && !raw_recording.empty()) {
            speech_active = 0;
            backchannel_armed = 0;

            if (first_turn) {
                first_turn = 0;
                fprintf(stdout, "[MIC] Caller detected\n");
                fprintf(stdout, "  [BOT] %s\n", greeting);
                fflush(stdout);
                tts_speak(greeting);
                llm_stream_add_message("assistant", greeting);
                if (!autolistening)
                    audio_capture_flush();

                raw_recording.clear();
                prebuf_init(&prebuf);
                vad_reset();
                continue;
            }

            struct timespec phase_timer;

            /* Phase 1: VAD + downsample */
            timer_start(&phase_timer);
            int raw_count = (int)raw_recording.size();
            int ds_max = raw_count / DOWNSAMPLE_RATIO + 1;
            std::vector<int16_t> recording_16k(ds_max);
            int ds_count = 0;
            downsample_filtered(raw_recording.data(), raw_count,
                                recording_16k.data(), &ds_count);
            timer_stop(&phase_timer, "VAD + downsample");

            fprintf(stdout, "[MIC] Speech detected (%d samples, %.1fs)\n",
                    ds_count, (float)ds_count / WHISPER_SAMPLE_RATE);
            fflush(stdout);

            struct WhisperJob {
                const int16_t *samples;
                int            sample_count;
                const void    *wav;
                size_t         wav_size;
                char          *text;
                int            result;
                int            local;
                struct timespec timer;
            };

            WhisperJob wjob;
            wjob.samples      = recording_16k.data();
            wjob.sample_count = ds_count;
            wjob.wav           = nullptr;
            wjob.wav_size      = 0;
            wjob.text          = nullptr;
            wjob.result        = -1;
            wjob.local         = use_whisper_local;

            uint8_t *wav_data = nullptr;
            if (!use_whisper_local) {
                size_t wav_sz = 0;
                wav_build(recording_16k.data(), ds_count,
                          WHISPER_SAMPLE_RATE, 1, &wav_data, &wav_sz);
                wjob.wav      = wav_data;
                wjob.wav_size = wav_sz;
            }

            if (use_whisper_local || wav_data) {
                timer_start(&wjob.timer);

                pthread_t whisper_thread;
                pthread_create(&whisper_thread, nullptr, [](void *arg) -> void * {
                    auto *j = (WhisperJob *)arg;
                    if (j->local)
                        j->result = whisper_local_transcribe(j->samples, j->sample_count, &j->text);
                    else
                        j->result = whisper_api_transcribe(j->wav, j->wav_size, &j->text);
                    timer_stop(&j->timer, j->local ? "Whisper STT (local)" : "Whisper STT (API)");
                    return nullptr;
                }, &wjob);

                if (use_fillers) {
                    const char *filler = fillers_thinking();
                    usleep(FILLER_DELAY_MS * 1000);
                    fprintf(stderr, "  [~] %s\n", filler);

                    timer_start(&phase_timer);
                    tts_speak(filler);
                    timer_stop(&phase_timer, "TTS (filler)");

                    llm_stream_add_message("assistant", filler);
                    if (!autolistening)
                        audio_capture_flush();
                }

                pthread_join(whisper_thread, nullptr);
                free(wav_data);

                if (wjob.result == 0 && wjob.text) {
                    fprintf(stdout, "[YOU] %s\n", wjob.text);
                    fflush(stdout);

                    if (strlen(wjob.text) > 0) {
                        llm_stream_chat(wjob.text, on_sentence, &autolistening);

                        fprintf(stdout, "\n");
                        fflush(stdout);

                        if (!autolistening) {
                            prebuf_init(&prebuf);
                            vad_reset();
                        }
                    }
                    free(wjob.text);
                } else {
                    fprintf(stderr, "[whisper] Transcription failed\n");
                }
            }

            raw_recording.clear();
            prebuf_init(&prebuf);
            vad_reset();
        }
    }

    fprintf(stderr, "\nShutting down...\n");

    llm_stream_free();
    if (use_whisper_local)
        whisper_local_free();
    else
        whisper_api_free();
    noise_reduce_free();
    audio_capture_close();

    return 0;
}
