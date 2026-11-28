#ifndef M4_RECOIL_H
#define M4_RECOIL_H

#include <math.h>

#define RECOIL_STEP_MS 4
#define RECOIL_TICK_S (1.0 / 64.0)
#define RECOIL_TABLE_SIZE 64
#define RECOIL_SUPPRESSION_SHOTS 4
#define RECOIL_SUPPRESSION_FACTOR 0.75
#define RECOIL_VARIANCE 0.55
#define RECOIL_INDEX_DECAY_PER_S 4.605170186
#define RECOIL_INDEX_IDLE_CYCLES 1.1
#define RECOIL_DEGREES (3.14159265358979323846 / 180.0)

typedef struct {
    int seed;
    double angle, angle_variance, magnitude, magnitude_variance;
} recoil_weapon;

static const recoil_weapon RECOIL_M4A4 = {38965, 0, 70, 23, 0};

typedef struct {
    double camera_scale, shake_scale, counts_per_degree, cycle_ms;
    double settle_deg_s, shake_decay, ceiling_deg;
    double bullet_scale, inaccuracy_scale;
} recoil_tuning;

static const recoil_tuning RECOIL_CS_TUNING = {1.0, 1.0, 1.0 / 0.044, 90, 1100 * 0.044, 50, 360.0, 1.0, 1.0};

#define RECOIL_SETTLE_TAU_MS 80.0
#define RECOIL_SETTLE_RAMP_MS 70.0
#define RECOIL_PLAYER_MOVE_DEG 0.25
#define RECOIL_STILL_DEG 0.05
#define RECOIL_RELEASE_CYCLES 1.3
#define RECOIL_HISTORY 128
#define RECOIL_LAG_STEPS_MAX 24
#define RECOIL_LAG_STEPS_START 8
#define RECOIL_LAG_JITTER_STEPS 2
#define RECOIL_LAG_FIT 64
#define RECOIL_LAG_SPAN 4
#define RECOIL_LAG_FIT_RANGE_DEG 0.3
#define RECOIL_HAND_NOISE_DEG 0.15

#define RECOIL_CLIMB_DEG 0.3
#define RECOIL_HOLD_RISE_MS 30.0
#define RECOIL_TOP_SETTLE_MS 40.0
#define RECOIL_INACCURACY_STAND 0.0049
#define RECOIL_INACCURACY_FIRE 0.007
#define RECOIL_SPREAD 0.0006
#define RECOIL_RECOVERY_S 0.338941
#define RECOIL_RECOVERY_FINAL_S 0.466044
#define RECOIL_RECOVERY_FROM_SHOT 2
#define RECOIL_RECOVERY_TO_SHOT 5

typedef struct {
    double angle, magnitude;
} recoil_entry;

typedef struct {
    double pitch, yaw;
} recoil_angle;

typedef struct {
    recoil_entry table[RECOIL_TABLE_SIZE];
    recoil_angle climb, last_climb, climb_from, shake, shake_velocity, last_shake;
    double peak_pitch;
    double index, last_shot_at, ticked_at, applied_side, applied_up, penalty, settle_speed;
    double base_up, sent_up, hand_anchor, hand_floor, budget, release_low, release_high, release_up, seen[RECOIL_HISTORY], sent[RECOIL_HISTORY];
    unsigned random;
    int shots, idle, home_known, stopped_at_home, released, history_head, history_count, lag_steps, lag_fitted;
} recoil_state;

static inline double recoil_unit_random(recoil_state *s) {
    s->random ^= s->random << 13;
    s->random ^= s->random >> 17;
    s->random ^= s->random << 5;
    return (s->random & 0xffffff) / (double)0x1000000;
}

typedef struct {
    int idum, iy, iv[32];
} recoil_random;

static inline int recoil_random_next(recoil_random *r) {
    enum { IA = 16807, IM = 2147483647, IQ = 127773, IR = 2836, NTAB = 32, NDIV = 1 + (IM - 1) / NTAB };
    int k;
    if (r->idum <= 0 || !r->iy) {
        r->idum = -r->idum < 1 ? 1 : -r->idum;
        for (int j = NTAB + 7; j >= 0; j--) {
            k = r->idum / IQ;
            r->idum = IA * (r->idum - k * IQ) - IR * k;
            if (r->idum < 0) r->idum += IM;
            if (j < NTAB) r->iv[j] = r->idum;
        }
        r->iy = r->iv[0];
    }
    k = r->idum / IQ;
    r->idum = IA * (r->idum - k * IQ) - IR * k;
    if (r->idum < 0) r->idum += IM;
    int j = r->iy / NDIV;
    r->iy = r->iv[j];
    r->iv[j] = r->idum;
    return r->iy;
}

static inline double recoil_random_float(recoil_random *r, double low, double high) {
    float fl = (float)(1.0 / 2147483647.0) * (float)recoil_random_next(r);
    if (fl > 1.0f - 1.2e-7f) fl = 1.0f - 1.2e-7f;
    return fl * (high - low) + low;
}

static inline double recoil_lerp(double t, double a, double b) {
    return a + (b - a) * t;
}

static inline void recoil_table(const recoil_weapon *w, recoil_entry *table) {
    recoil_random r = {w->seed < 0 ? w->seed : -w->seed, 0, {0}};
    double angle = 0, magnitude = 0;
    for (int j = 0; j < RECOIL_TABLE_SIZE; j++) {
        double next_angle = w->angle + recoil_random_float(&r, -w->angle_variance, w->angle_variance);
        double next_magnitude = w->magnitude + recoil_random_float(&r, -w->magnitude_variance, w->magnitude_variance);
        angle = j > 0 ? recoil_lerp(RECOIL_VARIANCE, angle, next_angle) : next_angle;
        magnitude = j > 0 ? recoil_lerp(RECOIL_VARIANCE, magnitude, next_magnitude) : next_magnitude;
        if (j < RECOIL_SUPPRESSION_SHOTS)
            magnitude *= recoil_lerp((double)j / RECOIL_SUPPRESSION_SHOTS, RECOIL_SUPPRESSION_FACTOR, 1.0);
        table[j].angle = angle;
        table[j].magnitude = magnitude;
    }
}

static inline void recoil_zero(recoil_state *s) {
    recoil_angle zero = {0, 0};
    s->climb = s->last_climb = s->climb_from = s->shake = s->shake_velocity = s->last_shake = zero;
    s->peak_pitch = 0;
    s->settle_speed = 0;
}

static inline void recoil_init(recoil_state *s, const recoil_weapon *w) {
    recoil_table(w, s->table);
    recoil_zero(s);
    s->index = 0;
    s->last_shot_at = -1e9;
    s->ticked_at = 0;
    s->applied_side = s->applied_up = 0;
    s->penalty = 0;
    s->random = 2463534242u;
    s->shots = 0;
    s->idle = 1;
    s->base_up = s->sent_up = s->hand_anchor = s->hand_floor = s->budget = s->release_low = s->release_high = s->release_up = 0;
    s->history_head = s->history_count = 0;
    s->lag_steps = RECOIL_LAG_STEPS_START;
    s->lag_fitted = 0;
    s->home_known = 0;
    s->stopped_at_home = 0;
    s->released = 0;
}

static inline void recoil_settle(recoil_state *s, const recoil_tuning *t, double dt) {
    double owed = sqrt(s->climb.pitch * s->climb.pitch + s->climb.yaw * s->climb.yaw), cap = t->settle_deg_s, dt_ms = dt * 1000.0;
    if (owed < 1e-6) {
        s->climb.pitch = s->climb.yaw = s->settle_speed = 0;
        return;
    }
    double rate = owed / RECOIL_SETTLE_TAU_MS * 1000.0;
    if (cap > 0 && rate > cap) rate = cap;
    if (cap > 0 && rate > s->settle_speed + cap * dt_ms / RECOIL_SETTLE_RAMP_MS) rate = s->settle_speed + cap * dt_ms / RECOIL_SETTLE_RAMP_MS;
    s->settle_speed = rate;
    double left = owed - rate * dt;
    if (left < 0.5 / t->counts_per_degree) left = 0;
    s->climb.pitch *= left / owed;
    s->climb.yaw *= left / owed;
}

static inline void recoil_spring(double *x, double *v, double omega, double dt) {
    double keep = exp(-omega * dt), a = *v + omega * *x;
    *x = (*x + a * dt) * keep;
    *v = (*v - omega * a * dt) * keep;
}

static inline int recoil_firing(const recoil_state *s, const recoil_tuning *t, double now) {
    return now <= s->last_shot_at + t->cycle_ms * RECOIL_INDEX_IDLE_CYCLES;
}

static inline void recoil_tick(recoil_state *s, const recoil_tuning *t, double tick_end_ms) {
    double dt = RECOIL_TICK_S;
    s->last_climb = s->climb;
    s->last_shake = s->shake;
    recoil_spring(&s->shake.pitch, &s->shake_velocity.pitch, t->shake_decay, dt);
    recoil_spring(&s->shake.yaw, &s->shake_velocity.yaw, t->shake_decay, dt);
    if (recoil_firing(s, t, tick_end_ms)) {
        s->settle_speed = 0;
    } else {
        s->index *= exp(-RECOIL_INDEX_DECAY_PER_S * dt);
        if (s->released) recoil_settle(s, t, dt);
    }
    double blend = (s->index - RECOIL_RECOVERY_FROM_SHOT) / (RECOIL_RECOVERY_TO_SHOT - RECOIL_RECOVERY_FROM_SHOT);
    if (blend < 0) blend = 0;
    if (blend > 1) blend = 1;
    s->penalty *= exp(-2.302585093 * dt / recoil_lerp(blend, RECOIL_RECOVERY_S, RECOIL_RECOVERY_FINAL_S));
}

static inline double recoil_tick_fraction(const recoil_state *s, double now) {
    double f = (now - s->ticked_at) / (RECOIL_TICK_S * 1000.0);
    return f < 0 ? 0 : f > 1 ? 1 : f;
}

static inline recoil_angle recoil_view(const recoil_state *s, double now) {
    double since = now - s->last_shot_at;
    if (since < 0) return s->climb_from;
    if (since < RECOIL_HOLD_RISE_MS) {
        double e = since / RECOIL_HOLD_RISE_MS;
        e = 1 - (1 - e) * (1 - e);
        recoil_angle c = {recoil_lerp(e, s->climb_from.pitch, s->peak_pitch), recoil_lerp(e, s->climb_from.yaw, s->climb.yaw)};
        return c;
    }
    if (since < RECOIL_HOLD_RISE_MS + RECOIL_TOP_SETTLE_MS) {
        double e = (since - RECOIL_HOLD_RISE_MS) / RECOIL_TOP_SETTLE_MS;
        e = e * e * (3 - 2 * e);
        recoil_angle c = {recoil_lerp(e, s->peak_pitch, s->climb.pitch), s->climb.yaw};
        return c;
    }
    double f = recoil_tick_fraction(s, now);
    recoil_angle c = {recoil_lerp(f, s->last_climb.pitch, s->climb.pitch), recoil_lerp(f, s->last_climb.yaw, s->climb.yaw)};
    return c;
}

static inline recoil_angle recoil_camera(const recoil_state *s, double now) {
    double f = recoil_tick_fraction(s, now);
    recoil_angle c = recoil_view(s, now);
    c.pitch += recoil_lerp(f, s->last_shake.pitch, s->shake.pitch);
    c.yaw += recoil_lerp(f, s->last_shake.yaw, s->shake.yaw);
    return c;
}

static inline recoil_angle recoil_bullet_offset(recoil_state *s, const recoil_tuning *t, double now) {
    double inaccuracy = (RECOIL_INACCURACY_STAND + s->penalty) * t->inaccuracy_scale;
    double r0 = recoil_unit_random(s) * inaccuracy, a0 = recoil_unit_random(s) * 2 * 3.14159265358979323846;
    double r1 = recoil_unit_random(s) * RECOIL_SPREAD * t->inaccuracy_scale, a1 = recoil_unit_random(s) * 2 * 3.14159265358979323846;
    double f = recoil_tick_fraction(s, now), extra = t->camera_scale > 0 ? t->bullet_scale / t->camera_scale - 1 : 0;
    recoil_angle o;
    o.pitch = extra * s->climb.pitch - recoil_lerp(f, s->last_shake.pitch, s->shake.pitch) - atan(r0 * sin(a0) + r1 * sin(a1)) / RECOIL_DEGREES;
    o.yaw = extra * s->climb.yaw - recoil_lerp(f, s->last_shake.yaw, s->shake.yaw) - atan(r0 * cos(a0) + r1 * cos(a1)) / RECOIL_DEGREES;
    return o;
}

static inline void recoil_keep_share(recoil_state *s, const recoil_tuning *t, double keep) {
    s->applied_up += -s->climb.pitch * (1 - keep) * t->counts_per_degree;
    s->applied_side += s->climb.yaw * (1 - keep) * t->counts_per_degree;
    s->climb.pitch *= keep;
    s->climb.yaw *= keep;
    s->last_climb = s->climb_from = s->climb;
    s->peak_pitch = s->climb.pitch;
}

static inline double recoil_sent_ago(const recoil_state *s, int steps) {
    if (s->history_count == 0) return s->sent_up;
    if (steps >= s->history_count) steps = s->history_count - 1;
    if (steps < 0) steps = 0;
    return s->sent[(s->history_head - 1 - steps + RECOIL_HISTORY) % RECOIL_HISTORY];
}

static inline double recoil_hand(const recoil_state *s, double view_up, double *low, double *high) {
    double least = 1e9, most = -1e9;
    int from = s->lag_fitted ? s->lag_steps - RECOIL_LAG_JITTER_STEPS : 0;
    int to = s->lag_fitted ? s->lag_steps + RECOIL_LAG_JITTER_STEPS : RECOIL_LAG_STEPS_MAX;
    for (int k = from; k <= to; k++) {
        double was = recoil_sent_ago(s, k);
        if (was < least) least = was;
        if (was > most) most = was;
    }
    double hand = view_up - s->base_up - s->hand_anchor;
    *low = hand - most;
    *high = hand - least;
    return most - least;
}

static inline void recoil_fit_lag(recoil_state *s) {
    if (s->history_count < RECOIL_LAG_FIT + RECOIL_LAG_STEPS_MAX + RECOIL_LAG_SPAN) return;
    double least = 1e9, most = -1e9;
    for (int i = 0; i < RECOIL_LAG_FIT + RECOIL_LAG_STEPS_MAX; i++) {
        double was = recoil_sent_ago(s, i);
        if (was < least) least = was;
        if (was > most) most = was;
    }
    if (most - least < RECOIL_LAG_FIT_RANGE_DEG) return;
    double best = 1e18;
    for (int k = 0; k <= RECOIL_LAG_STEPS_MAX; k++) {
        double sum = 0, square = 0;
        for (int i = 0; i < RECOIL_LAG_FIT; i++) {
            double seen = s->seen[(s->history_head - 1 - i + RECOIL_HISTORY) % RECOIL_HISTORY] -
                          s->seen[(s->history_head - 1 - i - RECOIL_LAG_SPAN + 2 * RECOIL_HISTORY) % RECOIL_HISTORY];
            double gap = seen - (recoil_sent_ago(s, i + k) - recoil_sent_ago(s, i + k + RECOIL_LAG_SPAN));
            sum += gap;
            square += gap * gap;
        }
        double spread = square / RECOIL_LAG_FIT - (sum / RECOIL_LAG_FIT) * (sum / RECOIL_LAG_FIT);
        if (spread < best - 1e-12) {
            best = spread;
            s->lag_steps = k;
        }
    }
    s->lag_fitted = 1;
}

static inline void recoil_remember(recoil_state *s, int view_known, double view_up) {
    if (!view_known) {
        s->history_count = 0;
        return;
    }
    s->seen[s->history_head] = view_up;
    s->sent[s->history_head] = s->sent_up;
    s->history_head = (s->history_head + 1) % RECOIL_HISTORY;
    if (s->history_count < RECOIL_HISTORY) s->history_count++;
    recoil_fit_lag(s);
}

static inline recoil_angle recoil_fire(recoil_state *s, const recoil_tuning *t, double now, int view_known, double view_up) {
    if (s->idle) {
        s->home_known = view_known;
        s->base_up = view_up;
        s->sent_up = s->hand_anchor = s->hand_floor = 0;
        s->budget = t->ceiling_deg;
        s->history_count = 0;
        s->stopped_at_home = 0;
        double from = s->last_shot_at + t->cycle_ms * RECOIL_INDEX_IDLE_CYCLES;
        if (s->ticked_at > from) from = s->ticked_at;
        if (now > from) s->index *= exp(-RECOIL_INDEX_DECAY_PER_S * (now - from) / 1000.0);
        s->idle = 0;
        s->ticked_at = now;
    }
    if (s->released) {
        double still_up = -recoil_view(s, now).pitch, returned = s->release_up - (still_up > 0 ? still_up : 0);
        if (returned > 0) s->budget = fmin(t->ceiling_deg, s->budget + returned);
    }
    s->released = 0;
    s->climb_from = recoil_view(s, now);
    s->climb = s->last_climb = s->climb_from;
    if (view_known && s->home_known) {
        double low, high;
        recoil_hand(s, view_up, &low, &high);
        if (low > s->hand_floor + RECOIL_HAND_NOISE_DEG) {
            double up = low - s->hand_floor - RECOIL_HAND_NOISE_DEG, regain = fmin(up, -s->climb.pitch);
            if (regain > 0) {
                s->applied_up += regain * t->counts_per_degree;
                s->climb.pitch += regain;
                s->last_climb.pitch = s->climb_from.pitch = s->climb.pitch;
            }
            s->budget += up;
            s->hand_anchor += up;
        } else if (high < s->hand_floor - RECOIL_HAND_NOISE_DEG) {
            s->budget += s->hand_floor - RECOIL_HAND_NOISE_DEG - high;
            s->hand_floor = high + RECOIL_HAND_NOISE_DEG;
        }
        if (s->budget > t->ceiling_deg) s->budget = t->ceiling_deg;
    }
    recoil_angle bullet = recoil_bullet_offset(s, t, now);
    s->penalty += RECOIL_INACCURACY_FIRE;
    const recoil_entry *e = &s->table[((int)s->index) % RECOIL_TABLE_SIZE];
    double side = sin(e->angle * RECOIL_DEGREES), up = cos(e->angle * RECOIL_DEGREES);
    double step = RECOIL_CLIMB_DEG * t->camera_scale * e->magnitude / RECOIL_M4A4.magnitude;
    double rise = up * step, kick = rise;
    if (rise > s->budget) rise = s->budget > 0 ? s->budget : 0;
    s->budget -= rise;
    s->climb.yaw -= side * step;
    s->climb.pitch -= rise;
    s->peak_pitch = s->climb_from.pitch - kick;
    double snap = e->magnitude * 0.055 * t->shake_scale * t->shake_decay * 2.718281828459045;
    s->shake_velocity.yaw -= side * snap;
    s->shake_velocity.pitch -= up * snap;
    s->index += 1;
    s->last_shot_at = now;
    s->shots++;
    return bullet;
}

static inline void recoil_reset_index(recoil_state *s) {
    s->index = 0;
}

static inline int recoil_busy(const recoil_state *s) {
    double c = fabs(s->climb.pitch) + fabs(s->climb.yaw) + fabs(s->last_climb.pitch) + fabs(s->last_climb.yaw);
    double k = fabs(s->shake.pitch) + fabs(s->shake.yaw) + fabs(s->last_shake.pitch) + fabs(s->last_shake.yaw) +
               (fabs(s->shake_velocity.pitch) + fabs(s->shake_velocity.yaw)) * RECOIL_TICK_S;
    return !s->idle && (c > 1e-6 || k > 1e-3);
}

static inline int recoil_round(double wanted, double applied) {
    double d = wanted - applied;
    return (int)(d + (d > 0 ? 0.5 : -0.5));
}

static inline void recoil_step(recoil_state *s, const recoil_tuning *t, double now, int view_known, double view_up, int *move_side,
                               int *move_up) {
    double tick_ms = RECOIL_TICK_S * 1000.0;
    *move_side = *move_up = 0;
    if (s->idle) return;
    while (now >= s->ticked_at + tick_ms) {
        s->ticked_at += tick_ms;
        recoil_tick(s, t, s->ticked_at);
    }
    recoil_remember(s, view_known, view_up);
    if (now > s->last_shot_at + t->cycle_ms * RECOIL_RELEASE_CYCLES && !s->released) {
        double low = 0, high = 0, spread = 0;
        int known = s->home_known && view_known;
        if (known) spread = recoil_hand(s, view_up, &low, &high);
        if (!known || spread < RECOIL_STILL_DEG || now > s->last_shot_at + t->cycle_ms * RECOIL_INDEX_IDLE_CYCLES + 2 * RECOIL_LAG_STEPS_MAX * RECOIL_STEP_MS) {
            s->released = 1;
            s->release_low = low;
            s->release_high = high;
            double climbed = -s->climb.pitch, hand = (low + high) / 2, room = climbed + (hand < 0 ? hand : 0);
            if (known && climbed > 1e-6 && room < climbed) {
                recoil_keep_share(s, t, room > 0 ? room / climbed : 0);
                s->stopped_at_home = 1;
            }
            s->release_up = -s->climb.pitch;
        }
    } else if (s->released && s->home_known && view_known && fabs(s->climb.pitch) + fabs(s->climb.yaw) > 0) {
        double low, high;
        recoil_hand(s, view_up, &low, &high);
        if (low > s->release_high + RECOIL_PLAYER_MOVE_DEG || high < s->release_low - RECOIL_PLAYER_MOVE_DEG) {
            s->release_up += s->climb.pitch;
            recoil_keep_share(s, t, 0);
            s->stopped_at_home = 1;
        }
    }
    if (!recoil_busy(s) && !recoil_firing(s, t, now)) {
        recoil_zero(s);
        s->applied_side = s->applied_up = 0;
        s->idle = 1;
        return;
    }
    recoil_angle c = recoil_camera(s, now);
    double wanted_up = c.pitch * t->counts_per_degree;
    double wanted_side = -c.yaw * t->counts_per_degree;
    *move_up = recoil_round(wanted_up, s->applied_up);
    *move_side = recoil_round(wanted_side, s->applied_side);
    s->applied_up += *move_up;
    s->applied_side += *move_side;
    s->sent_up -= *move_up / t->counts_per_degree;
}
#endif
