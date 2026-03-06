#ifndef ENV_LOADER_H
#define ENV_LOADER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Load KEY=VALUE pairs from a .env file and set them as environment variables.
 * Skips blank lines and lines starting with '#'.
 * Does NOT override variables already set in the environment.
 *
 * @param path  Path to the .env file
 * @return Number of variables loaded, or -1 if the file cannot be opened
 */
int env_load(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* ENV_LOADER_H */
