#include "fillers.h"

#include <stdlib.h>
#include <time.h>

#define BACKCHANNEL_MIN_MS  4000
#define BACKCHANNEL_MAX_MS  8000

static const char *backchannels[] = {
    "Ajá",
    "Sí",
    "Okey",
    "Claro",
    "Bien",
    "Dale",
    "Sí sí",
    "Entiendo",
    "Perfecto",
};

static const int backchannel_count =
    (int)(sizeof(backchannels) / sizeof(backchannels[0]));

static const char *thinking_fillers[] = {
    "Ooookey",
    "Bueeeenoooo",
    "Síiiiii",
};

static const int thinking_count =
    (int)(sizeof(thinking_fillers) / sizeof(thinking_fillers[0]));

void fillers_init(void)
{
    srand((unsigned int)time(NULL));
}

const char *fillers_backchannel(void)
{
    return backchannels[rand() % backchannel_count];
}

const char *fillers_thinking(void)
{
    return thinking_fillers[rand() % thinking_count];
}

int fillers_backchannel_interval_ms(void)
{
    int range = BACKCHANNEL_MAX_MS - BACKCHANNEL_MIN_MS;
    return BACKCHANNEL_MIN_MS + (rand() % (range + 1));
}
