#include "helpers.h"
#include <stdlib.h>
#include <time.h>

void simulate_delay(void) {
    struct timespec ts;
    ts.tv_sec=0;
    ts.tv_nsec=(rand()%50+1)*1000000L;
    nanosleep(&ts, NULL);
}
