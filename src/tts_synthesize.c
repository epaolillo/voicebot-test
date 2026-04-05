#include "tts_synthesize.h"
#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

static int write_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    size_t left = len;
    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += (size_t)n;
        left -= (size_t)n;
    }
    return 0;
}

int tts_synthesize_pcm_stream_start(const char *text, const char *model_full_path,
                                    float length_scale,
                                    const char *piper_bin, const char *piper_ld_path,
                                    const char *espeak_data,
                                    int *out_fd, pid_t *out_pid)
{
    if (!text || !model_full_path || !piper_bin || !out_fd || !out_pid)
        return -1;

    int in_pipe[2]  = {-1, -1};
    int out_pipe[2] = {-1, -1};

    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) {
        if (in_pipe[0] >= 0)  close(in_pipe[0]);
        if (in_pipe[1] >= 0)  close(in_pipe[1]);
        if (out_pipe[0] >= 0) close(out_pipe[0]);
        if (out_pipe[1] >= 0) close(out_pipe[1]);
        return -1;
    }

    char config_path[2048];
    snprintf(config_path, sizeof(config_path), "%s.json", model_full_path);

    char len_scale_str[32];
    char silence_str[32];
    snprintf(len_scale_str, sizeof(len_scale_str), "%.2f", (double)length_scale);
    snprintf(silence_str, sizeof(silence_str), "%.2f", (double)PIPER_SENTENCE_SILENCE);

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        close(in_pipe[1]);
        close(out_pipe[0]);

        if (dup2(in_pipe[0], STDIN_FILENO) < 0)
            _exit(126);
        if (dup2(out_pipe[1], STDOUT_FILENO) < 0)
            _exit(126);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        close(in_pipe[0]);
        close(out_pipe[1]);

        if (piper_ld_path && piper_ld_path[0])
            setenv("LD_LIBRARY_PATH", piper_ld_path, 1);

        char *argv[24];
        int ai = 0;
        argv[ai++] = (char *)piper_bin;
        argv[ai++] = "--model";
        argv[ai++] = (char *)model_full_path;
        argv[ai++] = "--config";
        argv[ai++] = config_path;
        if (espeak_data && espeak_data[0]) {
            argv[ai++] = "--espeak_data";
            argv[ai++] = (char *)espeak_data;
        }
        argv[ai++] = "--length_scale";
        argv[ai++] = len_scale_str;
        argv[ai++] = "--sentence_silence";
        argv[ai++] = silence_str;
        argv[ai++] = "--output_raw";
        argv[ai++] = "--quiet";
        argv[ai++] = NULL;

        execv(piper_bin, argv);
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);

    size_t tlen = strlen(text);
    if (write_all(in_pipe[1], text, tlen) != 0) {
        close(in_pipe[1]);
        close(out_pipe[0]);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }
    close(in_pipe[1]);

    *out_fd  = out_pipe[0];
    *out_pid = pid;
    return 0;
}

int tts_synthesize_pcm_collect(const char *text, const char *model_full_path,
                               float length_scale,
                               const char *piper_bin, const char *piper_ld_path,
                               const char *espeak_data,
                               uint8_t **pcm_out, size_t *pcm_size)
{
    if (!pcm_out || !pcm_size)
        return -1;

    *pcm_out  = NULL;
    *pcm_size = 0;

    int fd;
    pid_t pid;
    if (tts_synthesize_pcm_stream_start(text, model_full_path, length_scale,
                                         piper_bin, piper_ld_path, espeak_data,
                                         &fd, &pid) != 0)
        return -1;

    size_t cap = 65536;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) {
        close(fd);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }

    size_t total = 0;
    for (;;) {
        if (total + 16384 > cap) {
            cap = cap * 2 + 16384;
            uint8_t *nb = (uint8_t *)realloc(buf, cap);
            if (!nb) {
                free(buf);
                close(fd);
                kill(pid, SIGKILL);
                waitpid(pid, NULL, 0);
                return -1;
            }
            buf = nb;
        }
        ssize_t n = read(fd, buf + total, 16384);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            free(buf);
            close(fd);
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            return -1;
        }
        if (n == 0)
            break;
        total += (size_t)n;
    }

    close(fd);
    int st = 0;
    waitpid(pid, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        free(buf);
        return -1;
    }

    *pcm_out  = buf;
    *pcm_size = total;
    return 0;
}
