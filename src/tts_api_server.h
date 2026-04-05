#ifndef TTS_API_SERVER_H
#define TTS_API_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start OpenAI-compatible TTS HTTP API (blocks until SIGINT/SIGTERM or MHD_stop).
 * @param project_dir  Same base as voicebot (piper/, models/)
 * @param port         TCP port (e.g. 8080)
 * @param api_key      If non-NULL and non-empty, require Authorization: Bearer <api_key>
 * @return 0 on clean shutdown, -1 on fatal setup error
 */
int run_tts_api_server(const char *project_dir, int port, const char *api_key);

#ifdef __cplusplus
}
#endif

#endif /* TTS_API_SERVER_H */
