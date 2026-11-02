#include <stdio.h>
#include "m4_hip.h"

typedef struct {
    const char *name;
    int down;
    double pressed_at, released_at, now, shot_at;
    int free, wanted, action, down_after;
} HipCase;

static const HipCase CASES[] = {
    {"a click on a free hip light holds the charge", 0, 0, 0, 100, 0, 1, 1, HIP_PRESS, 1},
    {"no click holds nothing", 0, 0, 0, 100, 0, 1, 0, HIP_NONE, 0},
    {"a click on a busy gun holds nothing", 0, 0, 0, 100, 0, 0, 1, HIP_NONE, 0},
    {"a press right after a release waits so the game sees both", 0, 0, 95, 100, 0, 1, 1, HIP_NONE, 0},
    {"a press after the gap goes through", 0, 0, 40, 100, 0, 1, 1, HIP_PRESS, 1},
    {"a tap keeps the charge until the shot lands", 1, 100, 0, 300, 0, 1, 0, HIP_NONE, 1},
    {"a tap lets go after the shot's tail", 1, 100, 0, 900, 450, 1, 0, HIP_RELEASE, 0},
    {"the tail keeps quick taps charged", 1, 100, 0, 700, 450, 1, 0, HIP_NONE, 1},
    {"a held burst keeps the charge", 1, 100, 0, 5000, 4900, 1, 1, HIP_NONE, 1},
    {"a held burst a hit interrupted presses again", 1, 100, 0, 5000, 4000, 1, 1, HIP_RELEASE, 0},
    {"a charge that never shot retries", 1, 100, 0, 700, 0, 1, 1, HIP_RELEASE, 0},
    {"a shot from before the press does not count", 1, 100, 0, 300, 50, 1, 1, HIP_NONE, 1},
    {"a busy gun lets go at once", 1, 100, 0, 200, 0, 0, 1, HIP_RELEASE, 0},
};

int main(void) {
    int failed = 0;
    for (unsigned i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        const HipCase *c = &CASES[i];
        HipHold hold = {c->down, c->pressed_at, c->released_at};
        int action = hip_step(&hold, c->now, c->shot_at, c->free, c->wanted);
        int ok = action == c->action && hold.down == c->down_after;
        printf("%s %s\n", ok ? "ok" : "FAILED", c->name);
        failed += !ok;
    }
    return failed != 0;
}
