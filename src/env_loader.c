#include "env_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int env_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    char line[4096];
    int count = 0;

    while (fgets(line, sizeof(line), f)) {
        /* Strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        /* Skip empty lines and comments */
        if (len == 0 || line[0] == '#')
            continue;

        char *eq = strchr(line, '=');
        if (!eq)
            continue;

        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        /* Strip optional surrounding quotes from value */
        size_t vlen = strlen(val);
        if (vlen >= 2 &&
            ((val[0] == '"' && val[vlen - 1] == '"') ||
             (val[0] == '\'' && val[vlen - 1] == '\''))) {
            ((char *)val)[vlen - 1] = '\0';
            val++;
        }

        /* Don't override existing env vars */
        if (getenv(key) == NULL) {
            setenv(key, val, 0);
            count++;
        }
    }

    fclose(f);
    return count;
}
