#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *m4_string_cvar(void *console, const char *name, const char *start, const char *help);
void m4_cvar_set(void *cvar, const char *text);

#define SELECTOR_CVAR "m4_fire_key"
#define SELECTOR_CVAR_START "0"
#define SELECTOR_BUSY_CVAR "m4_selector_busy"
#define SELECTOR_BUSY_FRESH_MS 200
#define SELECTOR_SWING_CVAR "m4_selector_swing_"
#define SELECTOR_SWING_PARTS 6
#define SELECTOR_SWING_TURN 5729.577951308232
#define SELECTOR_SWING_STEP 10000.0
#define SELECTOR_CARRY_CVAR "m4_selector_carry"
#define SELECTOR_HOLD_MS 50.0
#define SELECTOR_FADE_MS 100.0

volatile LONG m4_selector_presses;
static void *selector_cvar;
static int selector_tried;
static LONG selector_written;
static volatile LONGLONG selector_busy_at, selector_swing_at, selector_carry_at;
static volatile LONG selector_swing[SELECTOR_SWING_PARTS];

static LONGLONG selector_ms(void) {
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return now.QuadPart * 1000 / frequency.QuadPart;
}

int m4_selector_busy(void) {
    LONGLONG at = InterlockedCompareExchange64(&selector_busy_at, 0, 0);
    return at && selector_ms() - at < SELECTOR_BUSY_FRESH_MS;
}

static double selector_fresh(volatile LONGLONG *stamp) {
    LONGLONG at = InterlockedCompareExchange64(stamp, 0, 0);
    if (!at) return 0;
    double idle = (double)(selector_ms() - at);
    return idle <= SELECTOR_HOLD_MS ? 1 : fmax(0, 1 - (idle - SELECTOR_HOLD_MS) / SELECTOR_FADE_MS);
}

double m4_selector_hip(void) {
    return selector_fresh(&selector_swing_at);
}

double m4_selector_swing(double *turn, double *step) {
    for (int i = 0; i < 3; i++) {
        turn[i] = InterlockedCompareExchange(&selector_swing[i], 0, 0) / SELECTOR_SWING_TURN;
        step[i] = InterlockedCompareExchange(&selector_swing[3 + i], 0, 0) / SELECTOR_SWING_STEP;
    }
    return m4_selector_hip();
}

double m4_selector_carry(void) {
    return selector_fresh(&selector_carry_at);
}

void m4_selector_console(void *console, const char *name) {
    if (name && !strcmp(name, SELECTOR_BUSY_CVAR)) InterlockedExchange64(&selector_busy_at, selector_ms());
    if (name && !strcmp(name, SELECTOR_CARRY_CVAR)) InterlockedExchange64(&selector_carry_at, selector_ms());
    if (name && !strncmp(name, SELECTOR_SWING_CVAR, sizeof(SELECTOR_SWING_CVAR) - 1)) {
        const char *at = name + sizeof(SELECTOR_SWING_CVAR) - 1;
        for (int i = 0; i < SELECTOR_SWING_PARTS; i++) {
            char *end;
            InterlockedExchange(&selector_swing[i], strtol(at, &end, 10));
            at = *end == '_' ? end + 1 : end;
        }
        InterlockedExchange64(&selector_swing_at, selector_ms());
    }
    if (!selector_cvar) {
        if (selector_tried) return;
        selector_tried = 1;
        selector_cvar = m4_string_cvar(console, SELECTOR_CVAR, SELECTOR_CVAR_START,
                                       "M4 fire selector: presses of its key while the M4 is drawn, set by m4_autofire.asi");
        if (!selector_cvar) return;
    }
    LONG presses = InterlockedCompareExchange(&m4_selector_presses, 0, 0);
    if (presses == selector_written) return;
    char text[16];
    snprintf(text, sizeof(text), "%ld", (long)presses);
    m4_cvar_set(selector_cvar, text);
    selector_written = presses;
}
