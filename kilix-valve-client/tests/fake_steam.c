#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;

static void
stop(int signal_number)
{
    (void)signal_number;
    stopping = 1;
}

int
main(void)
{
    const char *wayland = getenv("WAYLAND_DISPLAY");
    struct sigaction action;
    if (wayland == NULL || wayland[0] != '/' || getenv("DISPLAY") != NULL
            || getenv("XAUTHORITY") != NULL
            || getenv("LD_PRELOAD") != NULL
            || getenv("KILIX_VALVE_MANAGED") == NULL) {
        return 71;
    }
    (void)memset(&action, 0, sizeof(action));
    action.sa_handler = stop;
    (void)sigemptyset(&action.sa_mask);
    (void)sigaction(SIGTERM, &action, NULL);
    (void)sigaction(SIGINT, &action, NULL);
    while (!stopping) {
        (void)pause();
    }
    return 0;
}
