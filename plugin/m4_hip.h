#ifndef M4_HIP_H
#define M4_HIP_H

#include "m4_cadence.h"

enum { HIP_NONE, HIP_PRESS, HIP_RELEASE };
enum { HIP_IDLE, HIP_HELD, HIP_WAITING };
#define HIP_TAIL_MS 400
#define HIP_RETRY_MS 500
#define HIP_GAP_MS 60

typedef struct {
    volatile int down;
    double pressed_at, released_at;
} HipHold;

static int hip_step(HipHold *hold, double now, double shot_at, int free, int wanted) {
    if (!hold->down) {
        if (!free || !wanted || now - hold->released_at < HIP_GAP_MS) return HIP_NONE;
        hold->pressed_at = now;
        hold->down = 1;
        return HIP_PRESS;
    }
    int landed = shot_at > hold->pressed_at;
    double quiet = now - (landed ? shot_at : hold->pressed_at);
    if (free && quiet < (landed && !wanted ? HIP_TAIL_MS : HIP_RETRY_MS)) return HIP_NONE;
    hold->down = 0;
    hold->released_at = now;
    return HIP_RELEASE;
}

static int hip_aim(int aim, int free, int risen, int down) {
    if (down || (free && risen)) return AIM_READY;
    return free ? AIM_RAISING : aim;
}

static int hip_state(int free, int down, int wanted) {
    if (down) return HIP_HELD;
    return free && wanted ? HIP_WAITING : HIP_IDLE;
}

#endif
