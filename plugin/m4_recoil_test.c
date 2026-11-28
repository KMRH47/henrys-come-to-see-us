#include <math.h>
#include <stdio.h>
#include "m4_recoil.h"

static int failures;

static void check(const char *name, int ok) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) failures++;
}

typedef struct {
    double drag_per_shot, flick_up, move_after_release, lag_ms, frame_ms, wiggle, drag_until_shot;
} player;

typedef struct {
    double view[64], recoil[64], yaw[64];
    double end_ms, drop_between, peak_view, top_kick, release_view, release_hand, final_view, final_hand;
    int lag_steps;
} spray_result;

#define CEILING 3.0
#define FLICK_SHOT 15

static double spray_ceiling = CEILING;

static double hand_at(const player *p, double now, double cycle_ms, double last_shot) {
    double up = -p->drag_per_shot * fmin(now, fmin(last_shot, p->drag_until_shot * cycle_ms)) / cycle_ms;
    double flick_at = FLICK_SHOT * cycle_ms - 50;
    if (now > flick_at) up += p->flick_up * fmin(1, (now - flick_at) / 40);
    if (now > flick_at) up -= p->wiggle * fmin(fmin(1, (now - flick_at) / 40), fmax(0, (flick_at + 240 - now) / 40));
    if (now > last_shot + 120) up += p->move_after_release * fmin(1, (now - last_shot - 120) / 40);
    return up;
}

static spray_result spray(int shots, double cycle_ms, player p) {
    recoil_state s;
    recoil_tuning t = RECOIL_CS_TUNING;
    t.cycle_ms = cycle_ms;
    t.shake_scale = 0.15;
    t.ceiling_deg = spray_ceiling;
    recoil_init(&s, &RECOIL_M4A4);
    spray_result r = {{0}, {0}, {0}, -1, 0, -1e9, 0, 0, 0, 0, 0, 0};
    static double history[2048];
    int count = 0, k = 0, released = 0;
    double last_shot = (shots - 1) * cycle_ms, high = 0, top_high = -1e9, top_low = 1e9;
    long sum_up = 0, sum_side = 0;
    for (double now = 0; now < 5000; now += RECOIL_STEP_MS) {
        double hand = hand_at(&p, now, cycle_ms, last_shot), view = -sum_up / t.counts_per_degree + hand;
        history[count++] = view;
        double at = now - p.lag_ms;
        if (p.frame_ms > 0) at = floor(at / p.frame_ms) * p.frame_ms;
        int back = (int)floor(at / RECOIL_STEP_MS);
        double seen = history[back < 0 ? 0 : back >= count ? count - 1 : back];
        while (k < shots && k * cycle_ms <= now) {
            if (k < 64) {
                r.view[k] = view;
                r.recoil[k] = view - hand;
                r.yaw[k] = sum_side / t.counts_per_degree;
            }
            recoil_fire(&s, &t, k * cycle_ms, 1, seen);
            k++;
        }
        if (!released && !recoil_firing(&s, &t, now)) {
            released = 1;
            r.release_view = view;
            r.release_hand = hand;
        }
        int side, up;
        recoil_step(&s, &t, now, 1, seen, &side, &up);
        sum_up += up;
        sum_side += side;
        view = -sum_up / t.counts_per_degree + hand;
        if (view > r.peak_view) r.peak_view = view;
        double climbed = -recoil_view(&s, now).pitch;
        if (now >= 25 * cycle_ms && now < 26 * cycle_ms && shots > 26) {
            if (climbed > top_high) top_high = climbed;
            if (climbed < top_low) top_low = climbed;
            r.top_kick = top_high - top_low;
        }
        if (now <= 10 * cycle_ms && now <= last_shot) {
            if (climbed > high) high = climbed;
            if (high - climbed > r.drop_between) r.drop_between = high - climbed;
        }
        if (now > last_shot && s.idle && r.end_ms < 0) r.end_ms = now - last_shot;
        r.final_view = view;
        r.final_hand = hand;
    }
    r.lag_steps = s.lag_steps;
    return r;
}

static player still(double lag_ms) {
    player p = {0, 0, 0, lag_ms, lag_ms > 0 ? 16.7 : 0, 0, 1e9};
    return p;
}

int main(void) {
    recoil_entry table[RECOIL_TABLE_SIZE];
    recoil_table(&RECOIL_M4A4, table);
    static const struct {
        int shot;
        double angle, magnitude;
    } KICKS[] = {{1, -12.32, 17.25}, {2, -0.56, 16.59}, {10, 2.87, 22.97}, {14, 45.88, 23}, {30, 26.15, 23}};
    int kicks_ok = 1;
    for (size_t i = 0; i < sizeof(KICKS) / sizeof(KICKS[0]); i++) {
        const recoil_entry *e = &table[KICKS[i].shot - 1];
        kicks_ok &= fabs(e->angle - KICKS[i].angle) < 0.01 && fabs(e->magnitude - KICKS[i].magnitude) < 0.01;
    }
    check("the M4A4 kick table matches CS2's seed 38965", kicks_ok);

    static const double LAGS[] = {0, 40};
    for (int l = 0; l < 2; l++) {
        double lag = LAGS[l];
        char name[200];
        player p = still(lag);
        spray_result full = spray(30, 73, p);
#define CHECK(text, ok)                                                   \
    do {                                                                  \
        snprintf(name, sizeof(name), "%s (view %.0f ms late)", text, lag); \
        check(name, ok);                                                  \
    } while (0)
        CHECK("with no drag the view climbs, then stops at the ceiling",
              full.view[10] > 1.5 && full.view[29] > CEILING - 0.5 && full.view[29] < CEILING + 0.2 && full.peak_view < CEILING + 0.6);
        CHECK("below the ceiling the view never sinks by itself while the trigger is held", full.drop_between < 0.01);
        CHECK("at the ceiling every shot still kicks the view up and it settles back", full.top_kick > 0.2);
        spray_ceiling = 100;
        spray_result open = spray(30, 73, p);
        spray_ceiling = CEILING;
        double side_gap = 0, side_span = 0;
        for (int i = 0; i < 30; i++) {
            side_gap = fmax(side_gap, fabs(full.yaw[i] - open.yaw[i]));
            side_span = fmax(side_span, fabs(open.yaw[i]));
        }
        CHECK("the ceiling stops only the climb, the sideways path stays CS2's own pattern", side_gap < 0.01 && side_span > 0.5);
        CHECK("with no drag, letting go brings the view all the way back to the start", fabs(full.final_view) < 0.05);
        CHECK("the recoil goes idle within 1.5 s", full.end_ms > 0 && full.end_ms < 1500);

        p.drag_per_shot = 0.3;
        spray_result held = spray(30, 73, p);
        CHECK("while the player drags down, every shot still pulls up past the ceiling's worth", held.recoil[29] > CEILING + 1.5);
        CHECK("a player who dragged past the start sees no drop after letting go",
              held.release_view < 0 && fabs(held.final_view - held.release_view) < 0.1);
        p.drag_per_shot = 0.15;
        spray_result half = spray(30, 73, p);
        CHECK("a half-dragged spray returns only to the start pitch, never below it", half.release_view > 1 && half.final_view > -0.1 && half.final_view < 0.3);

        p = still(lag);
        p.drag_per_shot = -0.5;
        spray_result lifted = spray(30, 73, p);
        CHECK("aiming up during the spray never uses up the recoil, every shot keeps climbing", lifted.recoil[29] > 2 * CEILING && lifted.recoil[29] - lifted.recoil[20] > 1.5);

        p = still(lag);
        p.flick_up = 4;
        spray_result flick = spray(30, 73, p);
        CHECK("moving the aim up to a new spot at the ceiling starts the climb again from there",
              flick.view[29] - flick.view[FLICK_SHOT] > 0.8 * CEILING);
        CHECK("and letting go returns to the new spot, not the old start", fabs(flick.final_view - (CEILING + 4)) < 0.6);

        p = still(lag);
        p.drag_per_shot = 0.6;
        p.drag_until_shot = 10;
        spray_result dropped = spray(60, 73, p);
        CHECK("after dragging down and stopping, it climbs back only as far as the drag, one ceiling at most",
              dropped.view[59] - dropped.view[13] < CEILING + 0.3 && dropped.view[59] - dropped.view[13] > 1);

        p = still(lag);
        p.flick_up = 1;
        spray_result nudge = spray(30, 73, p);
        CHECK("a small move up at the ceiling buys back only that much climb", nudge.view[29] - nudge.view[FLICK_SHOT] > 0.6 && nudge.view[29] - nudge.view[FLICK_SHOT] < 1.4);

        p = still(lag);
        p.wiggle = 1;
        spray_result wiggle = spray(30, 73, p);
        CHECK("moving down and back up at the ceiling buys back no extra climb", wiggle.view[29] - wiggle.view[22] < 0.2);

        p = still(lag);
        p.move_after_release = -2;
        spray_result down = spray(30, 73, p);
        CHECK("pulling down during the return stops it, so the view does not keep going down with the player", down.final_view - down.final_hand > 0.5);
        p.move_after_release = 2;
        spray_result rise = spray(30, 73, p);
        CHECK("moving up during the return stops it too", rise.final_view - rise.final_hand > 0.5);

        spray_result one = spray(1, 90, still(lag));
        CHECK("one shot kicks the camera under 0.5 degree", one.peak_view < 0.5 && one.peak_view > 0);
        CHECK("and returns it fully", fabs(one.final_view) < 0.05);
        spray_result three = spray(3, 73, still(lag));
        CHECK("a 3-round burst returns fully", fabs(three.final_view) < 0.05);
    }
    {
        recoil_state s;
        recoil_tuning t = RECOIL_CS_TUNING;
        t.ceiling_deg = CEILING;
        t.shake_scale = 0.15;
        recoil_init(&s, &RECOIL_M4A4);
        long sum_up = 0;
        int busy_at_refire = 0;
        double again_from = 0, again_top = 0;
        for (double now = 0; now < 5000; now += RECOIL_STEP_MS) {
            double view = -sum_up / t.counts_per_degree;
            double again = 30 * 73 + 150;
            int second = now >= again && now < again + 30 * 73;
            if ((now < 30 * 73 || second) && fmod(now - (second ? again : 0), 73) < RECOIL_STEP_MS) {
                if (now < again + RECOIL_STEP_MS && second) {
                    again_from = view;
                    busy_at_refire = !s.idle;
                }
                recoil_fire(&s, &t, now, 1, view);
                if (second) again_top = view;
            }
            int side, up;
            recoil_step(&s, &t, now, 1, view, &side, &up);
            sum_up += up;
        }
        check("a second spray started during the return climbs back to the full ceiling, no higher", busy_at_refire && again_from > 0.1 * CEILING && fabs(again_top - CEILING) < 0.15 * CEILING);
    }
    spray_result late = spray(30, 73, still(40));
    check("the view's lag behind the mouse is measured from the spray", late.lag_steps >= 10 && late.lag_steps <= 14);

    recoil_state s;
    recoil_tuning t = RECOIL_CS_TUNING;
    recoil_init(&s, &RECOIL_M4A4);
    int side, up;
    for (int k = 0; k < 10; k++) {
        recoil_fire(&s, &t, k * 90.0, 0, 0);
        for (double now = k * 90.0; now < (k + 1) * 90.0; now += RECOIL_STEP_MS) recoil_step(&s, &t, now, 0, 0, &side, &up);
    }
    check("the index holds while firing at the cycle", s.index > 9.99);
    for (double now = 900; now < 810 + 250; now += RECOIL_STEP_MS) recoil_step(&s, &t, now, 0, 0, &side, &up);
    check("after 10 shots and a 0.25 s pause the pattern resumes near shot 5", s.index > 4.3 && s.index < 5.7);
    for (double now = 1060; now < 4000; now += RECOIL_STEP_MS) recoil_step(&s, &t, now, 0, 0, &side, &up);
    recoil_fire(&s, &t, 4000, 0, 0);
    check("a long pause starts the pattern over, even across an idle stretch", s.index > 0.99 && s.index < 1.01);
    recoil_reset_index(&s);
    check("a reload starts the pattern over", s.index == 0);
    return failures ? 1 : 0;
}
