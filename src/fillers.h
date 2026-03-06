#ifndef FILLERS_H
#define FILLERS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the random seed for filler selection.
 */
void fillers_init(void);

/**
 * Get a random backchannel word to play while the user is speaking.
 * Examples: "Ajá", "Sí", "Okey", "Claro"
 */
const char *fillers_backchannel(void);

/**
 * Get a random thinking filler to play while waiting for transcription/LLM.
 * Examples: "Emmm...", "Bueno mirá...", "A ver..."
 */
const char *fillers_thinking(void);

/**
 * Return a random interval in milliseconds between backchannel cues.
 * Range: BACKCHANNEL_MIN_MS to BACKCHANNEL_MAX_MS
 */
int fillers_backchannel_interval_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* FILLERS_H */
