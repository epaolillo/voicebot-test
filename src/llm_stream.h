#ifndef LLM_STREAM_H
#define LLM_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Callback invoked for each complete sentence from the LLM.
 * The text is a null-terminated string.
 */
typedef void (*llm_sentence_cb)(const char *sentence, void *userdata);

/**
 * Initialize the LLM streaming module.
 * @param api_key       OpenAI API key
 * @param system_prompt System prompt for the conversation
 * Returns 0 on success, -1 on error.
 */
int llm_stream_init(const char *api_key, const char *system_prompt);

/**
 * Send user text to the LLM and stream the response.
 * Calls sentence_cb for each complete sentence as it arrives.
 * Blocks until the full response is received.
 *
 * @param user_text   The user's transcribed speech
 * @param sentence_cb Called for each sentence fragment
 * @param userdata    Passed to sentence_cb
 * @return 0 on success, -1 on error
 */
int llm_stream_chat(const char *user_text, llm_sentence_cb sentence_cb, void *userdata);

/**
 * Manually add a message to the conversation history.
 * Use for greetings, fillers, or any text spoken outside of llm_stream_chat.
 *
 * @param role     "user" or "assistant"
 * @param content  The message text
 */
void llm_stream_add_message(const char *role, const char *content);

/**
 * Clear conversation history to start fresh.
 */
void llm_stream_clear_history(void);

/**
 * Free LLM resources.
 */
void llm_stream_free(void);

#ifdef __cplusplus
}
#endif

#endif /* LLM_STREAM_H */
