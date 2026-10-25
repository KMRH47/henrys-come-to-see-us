#ifndef M4_CADENCE_H
#define M4_CADENCE_H

#include <math.h>

enum { CADENCE_NONE, CADENCE_UP, CADENCE_DOWN };
enum { AIM_NONE, AIM_RAISING, AIM_READY };
#define CLICK_LATCH_MS 1500
#define HOLD_LIMIT_MS 250
#define FIRST_LAG_FRAMES 4

typedef struct {
    double now, shot_at, frame_ms, charged_at;
    long frame, shot_frame, clicks;
    int physical, enabled, aim, game_down;
} CadenceInput;

typedef struct {
    int active, down, waiting, latched, freed;
    long clicks, every, lag, pressed_frame, press_frame, release_frame;
    double requested, release_at, deadline, observed, clicked_at, slot;
} Cadence;

// frame_ms > 0 means the game is ticking the frame mark, and presses are counted
// in frames; without it they fall back to a millisecond clock.
static int cadence_step(Cadence *state, const CadenceInput *in, double period, double gap) {
    double now = in->now, shot_at = in->shot_at;
    int framed = in->frame_ms > 0;
    if (!in->enabled) {
        int action = CADENCE_NONE;
        if (state->active && state->down && !in->physical) action = CADENCE_UP;
        if (state->active && !state->down && in->physical) action = CADENCE_DOWN;
        *state = (Cadence){.clicks = in->clicks};
        return action;
    }
    if (in->clicks != state->clicks) {
        state->clicks = in->clicks;
        if (in->aim != AIM_NONE) {
            state->latched = 1;
            state->freed = 0;
            state->clicked_at = now;
        }
    }
    if (state->latched && (in->aim == AIM_NONE || now - state->clicked_at > CLICK_LATCH_MS)) state->latched = 0;
    if ((!in->physical && !state->latched) || in->aim == AIM_RAISING) {
        int release = state->down || (in->aim == AIM_RAISING && state->latched && !state->freed);
        *state = (Cadence){.clicks = in->clicks, .latched = state->latched, .clicked_at = state->clicked_at,
                           .freed = state->freed || release};
        return release ? CADENCE_UP : CADENCE_NONE;
    }
    if (!state->active) {
        if (in->game_down) {
            *state = (Cadence){.active = 1, .release_at = now, .release_frame = in->frame, .press_frame = in->frame + 2,
                               .deadline = now + gap, .observed = shot_at, .clicks = in->clicks,
                               .latched = state->latched, .clicked_at = state->clicked_at};
            return CADENCE_UP;
        }
        *state = (Cadence){.active = 1, .down = 1, .waiting = 1, .requested = now, .slot = now, .observed = shot_at,
                           .pressed_frame = in->frame, .clicks = in->clicks, .latched = state->latched,
                           .clicked_at = state->clicked_at};
        return CADENCE_DOWN;
    }
    if (shot_at >= state->requested && shot_at > state->observed) {
        state->observed = shot_at;
        state->waiting = 0;
        // Presses sit on a fixed clock from the first one, so the time the engine
        // takes to answer a press never adds to the next gap.
        double next = state->slot + period;
        state->deadline = next > shot_at ? next : shot_at + period;
        if (framed) {
            // Shots land on frames, so a millisecond clock gives gaps one or two
            // frames apart. The same number of frames every time gives one gap.
            double wanted = period / in->frame_ms;
            if (!state->every || fabs(wanted - state->every) > 0.75) state->every = wanted < 1.5 ? 1 : lround(wanted);
            // The engine answers a press one to three frames late. Timing from its
            // quickest answer keeps one late answer from also delaying the next.
            long lag = in->shot_frame - state->pressed_frame;
            if (!state->lag || lag < state->lag) state->lag = lag;
            state->press_frame = in->shot_frame + (state->every > state->lag ? state->every - state->lag : 1);
        }
        if (shot_at >= state->clicked_at) state->latched = 0;
    }
    if (state->down) {
        // The button stays down until the shot has landed. A release the game
        // sees before the gun fires drops that shot. A press still unanswered two
        // frames after the usual answer was refused, so it is let go and retried.
        long lag = state->lag > 0 ? state->lag : FIRST_LAG_FRAMES;
        int refused = framed ? in->frame - state->pressed_frame > lag + 2 : now - state->requested >= HOLD_LIMIT_MS;
        if (state->waiting ? !refused : !framed && now - state->requested < gap) return CADENCE_NONE;
        state->down = 0;
        state->release_at = now;
        state->release_frame = in->frame;
        if (state->waiting) {
            state->deadline = now + gap;
            state->press_frame = in->frame + 2;
        }
        return CADENCE_UP;
    }
    // The game must read the button up once between two presses, or it sees one
    // long press, so a press waits two frame marks after the release.
    int uncharged = in->charged_at > 0 && in->charged_at < state->observed && now - state->observed < 1.5 * period;
    if (framed ? in->frame < state->press_frame || in->frame < state->release_frame + 2 || uncharged
               : now < state->deadline || now - state->release_at < gap)
        return CADENCE_NONE;
    state->down = 1;
    state->waiting = 1;
    state->requested = now;
    state->pressed_frame = in->frame;
    state->slot = now - state->deadline < period ? state->deadline : now;
    return CADENCE_DOWN;
}

#endif
