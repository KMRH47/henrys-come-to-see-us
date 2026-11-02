#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

void note(const char *text);
unsigned char *near_page(unsigned char *base);
unsigned char *emit(unsigned char *at, const void *bytes, size_t length);
unsigned char *emit_call(unsigned char *at, void *target);
unsigned char *emit_jump(unsigned char *at, void *target);
void put_jump(unsigned char *from, unsigned char *to, size_t length);
void put_call(unsigned char *from, unsigned char *to, size_t length);

#define CAMERA_ADD_RVA 0x94d322
#define CAMERA_BONE_ADD_RVA 0x94d999
#define ATTACHMENT_ADD_RVA 0x423ae4
#define SLOT_DRAWN_RVA 0x49f9c1
#define SKINNING_SIGNATURE_RVA 0x5fe29f
#define SKINNING_CALL_RVA 0x5fe2ab
#define SKINNING_COMPUTATION_RVA 0x5fe2f0
#define ADD_LOOP_LENGTH 0x1c
#define SLOT_DRAWN_LENGTH 7
#define SKINNING_CALL_LENGTH 5
#define SLOT_CHARACTER 0x80
#define CONTEXT_SKELETON 0x08
#define CONTEXT_INSTANCE_PLUS_8 0x10
#define CONTEXT_BONE_QUATS 0x20
#define SKINNING_BONES 0x00
#define SKELETON_JOINT_COUNT_SLOT 1
#define MAX_SHIFTED_JOINTS 1024
static const unsigned char CAMERA_ADD_BYTES[] = {
    0xf3, 0x0f, 0x10, 0x44, 0x04, 0x30, 0xf3, 0x0f, 0x58, 0x44, 0x04, 0x40, 0xf3, 0x0f, 0x11, 0x44,
    0x04, 0x4c, 0x48, 0x83, 0xc0, 0x04, 0x48, 0x83, 0xf8, 0x0c, 0x7c, 0xe4
};
static const unsigned char ATTACHMENT_ADD_BYTES[] = {
    0xf3, 0x0f, 0x10, 0x44, 0x05, 0x97, 0xf3, 0x0f, 0x58, 0x44, 0x05, 0xbf, 0xf3, 0x0f, 0x11, 0x44,
    0x05, 0xa3, 0x48, 0x83, 0xc0, 0x04, 0x48, 0x83, 0xf8, 0x0c, 0x7c, 0xe4
};
static const unsigned char SLOT_DRAWN_BYTES[] = {0x48, 0x8d, 0x45, 0xc8, 0x4d, 0x8b, 0xc7};
static const unsigned char SKINNING_CALL_BYTES[] = {
    0x49, 0x8d, 0x4b, 0xc8, 0x49, 0x89, 0x43, 0xd8, 0x4d, 0x89, 0x43, 0xe0, 0xe8, 0x40, 0x00, 0x00,
    0x00, 0x48, 0x8b, 0x47, 0x50
};

static const struct { size_t rva; const unsigned char *bytes; size_t length; } precision_signatures[] = {
    {CAMERA_ADD_RVA, CAMERA_ADD_BYTES, sizeof(CAMERA_ADD_BYTES)},
    {CAMERA_BONE_ADD_RVA, CAMERA_ADD_BYTES, sizeof(CAMERA_ADD_BYTES)},
    {ATTACHMENT_ADD_RVA, ATTACHMENT_ADD_BYTES, sizeof(ATTACHMENT_ADD_BYTES)},
    {SLOT_DRAWN_RVA, SLOT_DRAWN_BYTES, sizeof(SLOT_DRAWN_BYTES)},
    {SKINNING_SIGNATURE_RVA, SKINNING_CALL_BYTES, sizeof(SKINNING_CALL_BYTES)},
};

static const unsigned char SAVE_VOLATILES_IN_0XC0[] = {
    0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, 0x48, 0x81, 0xec, 0x88, 0x00,
    0x00, 0x00, 0x0f, 0x11, 0x44, 0x24, 0x20, 0x0f, 0x11, 0x4c, 0x24, 0x30, 0x0f, 0x11, 0x54, 0x24,
    0x40, 0x0f, 0x11, 0x5c, 0x24, 0x50, 0x0f, 0x11, 0x64, 0x24, 0x60, 0x0f, 0x11, 0x6c, 0x24, 0x70
};
static const unsigned char RESTORE_VOLATILES[] = {
    0x0f, 0x10, 0x44, 0x24, 0x20, 0x0f, 0x10, 0x4c, 0x24, 0x30, 0x0f, 0x10, 0x54, 0x24, 0x40, 0x0f,
    0x10, 0x5c, 0x24, 0x50, 0x0f, 0x10, 0x64, 0x24, 0x60, 0x0f, 0x10, 0x6c, 0x24, 0x70, 0x48, 0x81,
    0xc4, 0x88, 0x00, 0x00, 0x00, 0x41, 0x5b, 0x41, 0x5a, 0x41, 0x59, 0x41, 0x58, 0x5a, 0x59, 0x58
};
static const unsigned char CAMERA_ARGS_RSP_30_40_4C[] = {
    0x48, 0x8d, 0x8c, 0x24, 0xf0, 0x00, 0x00, 0x00, 0x48, 0x8d, 0x94, 0x24, 0x00, 0x01, 0x00, 0x00,
    0x4c, 0x8d, 0x84, 0x24, 0x0c, 0x01, 0x00, 0x00
};
static const unsigned char CAMERA_LOOP_EXIT_STATE[] = {
    0xb8, 0x0c, 0x00, 0x00, 0x00, 0xf3, 0x0f, 0x10, 0x44, 0x24, 0x54, 0x48, 0x83, 0xf8, 0x0c
};
static const unsigned char ATTACHMENT_ARGS_RBP_69_41_5D[] = {
    0x48, 0x8d, 0x4d, 0x97, 0x48, 0x8d, 0x55, 0xbf, 0x4c, 0x8d, 0x45, 0xa3
};
static const unsigned char ATTACHMENT_LOOP_EXIT_STATE[] = {
    0xb8, 0x0c, 0x00, 0x00, 0x00, 0xf3, 0x0f, 0x10, 0x45, 0xab, 0x48, 0x83, 0xf8, 0x0c
};
static const unsigned char DRAWN_ARGS_MATRIX_RBP_38_SLOT_RSI_PASS_R13[] = {0x48, 0x8d, 0x4d, 0xc8, 0x48, 0x89, 0xf2, 0x4d, 0x89, 0xe8};
static const unsigned char SKINNING_ARGS_CONTEXT_RCX_DATA_RDI[] = {0x48, 0x89, 0xfa};

#define PRECISION_CAMERA 1
#define PRECISION_GUN 2
#define PRECISION_SIGHT 4
#define PRECISION_FOLLOW 8
#define PRECISION_HOLD 16
#define PRECISION_PIN 32
volatile LONG m4_sight_dot;
volatile LONG m4_precision = PRECISION_CAMERA | PRECISION_GUN | PRECISION_SIGHT | PRECISION_FOLLOW | PRECISION_HOLD | PRECISION_PIN;
volatile LONG m4_saddle_lift = 50, m4_saddle_push = 10, m4_saddle_ease = 60, m4_saddle_rate = 200;
volatile LONG m4_hip_right = 33, m4_hip_ahead = 83, m4_hip_up = -39, m4_aim_depth = 119;
volatile LONG m4_pin_body = 0;
volatile LONG m4_hit_gain = 100;
volatile LONG m4_sway_move = 20, m4_sway_tilt = 100, m4_sway_lean = 2, m4_sway_look = 15, m4_sway_aim = 25, m4_sway_hz = 220;
void *volatile m4_body_instance;
extern volatile LONG tag_armed;
double m4_since_shot_ms(void);
int m4_stance(void);
int m4_aim_held(void);

typedef struct {
    volatile LONG seq;
    volatile UINT32 key[3];
    volatile float lost[3];
} rounding;

#define CAMERA_RING 8
#define ATTACHMENT_RING 1024
typedef struct {
    volatile LONG seq;
    void *volatile instance;
    volatile float shift[3], sight[8];
    volatile LONG frame, kind, sighted;
} skin_shift;

#define SHIFT_GUN 1
#define SHIFT_BODY 2
#define KEEP_RADIUS_SQUARED 2.25f
#define SHIFTS 64
#define SHIFT_FRAMES 2
#define SHIFT_PROBE 8
#define SHIFT_READ_TRIES 64
#define PASS_SCAN_BYTES 0x100
#define PASS_SCAN_LIMIT 600
#define PASS_CAMERA_RADIUS_SQUARED 0.0025f
#define REPORT_MS 3000
#define DEGREES 57.29577951308232
#define SIGHT_FULL_DEG 15.0
#define SIGHT_NONE_DEG 25.0
#define SIGHT_REACH_M 0.08
#define SIGHT_NEAR_M 0.05
#define SIGHT_FAR_M 1.5
#define SIGHT_LEARN_DEG 3.0
#define SIGHT_LEARN_M 0.03
#define SIGHT_STEADY_DEG_S 3.0
#define SIGHT_GUN_DEG_S 5.0
#define SIGHT_FOLLOW_FROM_DEG_S 30.0
#define SIGHT_FOLLOW_FULL_DEG_S 90.0
#define SIGHT_FOLLOW_HOLD_S 0.3
#define SIGHT_FOLLOW_EASE_S 0.3
#define SIGHT_STANCE_LOCK_S 1.0
#define SIGHT_HOLD_S 0.5
#define SIGHT_ROLL_S 0.12
#define SIGHT_ROLL_CAP_DEG 20.0
#define SIGHT_STEADY_FRAMES 8
#define SIGHT_SHOT_QUIET_MS 600
#define VIEW_FRESH_S 0.1
#define SIGHT_TRACE_AFTER_SHOT_MS 3000
#define SIGHT_BIN_DEG 2.0
#define SIGHT_BINS 91
#define SIGHT_KINDS 4
#define SIGHT_LEARN_CAP 600.0
#define SIGHT_EASE_S 0.15
#define SIGHT_MOVING_M_S 0.6
#define SIGHT_SIMILAR 1e-3
#define SIGHT_BODY_WAIT_S 0.002
#define SADDLE_KICK_RISE_MS 25.0
#define SADDLE_KICK_FALL_MS 90.0
#define SADDLE_CATCH_S 0.5
#define SADDLE_STEADY_S 1.0
#define SADDLE_REST_DEG 0.01
#define SADDLE_REST_M 0.0001
#define SADDLE_SLIDE_M_S 1.5
#define BODY_OWED 8
#define SIGHT_DUMP_REPORTS 10
#define SIGHT_TRACE_FRAMES 512
#define SIGHT_ROWS 8192
#define ROW_FLUSH_MS 250
#define MARK_POLL_MS 20
#define MARK_KEY VK_F8
#define ROW_SOLVED 0
#define ROW_OUT_OF_REACH 2
#define ROW_OFF_LINE 3
#define ROW_MARK 9
#define STANCE_CROUCHED 1
#define STANCE_MOUNTED 2
#define PIN_TAU_S 0.06
#define PIN_DT_CAP_S 0.1
#define PIN_RISE_S 0.15
#define PIN_EASE_S 0.45
#define PIN_EASE_WINDOW_S 1.0
#define PIN_FALL_S 0.3
#define PIN_ROWS 8192
#define PIN_PATHS 8
#define PIN_TABLES 4
#define PIN_LEVELS 11
#define PIN_LEVEL_ARM 8
#define PIN_LEVEL_ELBOW 9
#define PIN_BODY_TAU_S 0.15
#define PIN_ARM_PULL 0.7f
#define PIN_ELBOW_PULL 0.6f
#define PIN_SADDLE 2
#define SWAY_CAMERA_TAU_S 0.05
#define SWAY_TELEPORT_M_S 30.0
#define SWAY_ACCEL_CAP 40.0
#define SWAY_STEP_S (1.0 / 240)
#define SWAY_DAMPING 0.5
#define SWAY_LIMIT_M 0.04
#define SWAY_LOOK_CAP_DEG 3.0
#define SWAY_ROLL 1.5
#define SWAY_DIP 0.5
#define SWAY_LOOK_ROLL 0.5
#define HIT_LAST_S 0.9
#define HIT_TAU_S 0.1
#define HIT_FULL_LOSS 10.0
#define HIT_SMALLEST 0.7
#define HIT_PITCH_DEG 6.0
#define HIT_ROLL_DEG 4.0
#define HIT_YAW_DEG 3.0
#define HIT_SIDE_M 0.01
#define HIT_BACK_M 0.02
#define HIT_DROP_M 0.02
#define CHARACTER_VTABLE_RVA 0x40602f8
#define CHARACTER_PATH_SLOT 31
#define CHARACTER_PATH_RVA 0x11a6690
#define SKELETON_VTABLE_RVA 0x406b930
#define SKELETON_PARENT_SLOT 2
#define SKELETON_PARENT_RVA 0x53f804
#define SKELETON_NAME_SLOT 6
#define SKELETON_NAME_RVA 0x5af5bc
static const double SIGHT_REAR[3] = {0.140185506235, -0.0745253730437, 0.187918731327};
static const double SIGHT_FRONT[3] = {0.1273731919, 0.0140441813561, 0.555570531152};
static const double DOT_REAR[3] = {0.142877491594, -0.0757738976918, 0.188300963487};
static const double DOT_FRONT[3] = {0.13006517726, 0.0127956567081, 0.555952763312};

typedef struct {
    double weight, value[6];
} sight_bin;

typedef struct {
    double at;
    float dt, view_yaw, view_pitch, gun_yaw, gun_pitch, raw_yaw, raw_pitch, gap, weight, follow, shot_s;
    int kind, learned;
} sight_trace;

typedef struct {
    double at;
    float dt, pitch, turn_s, angle, gap, weight, follow, shot_s, pull, view_yaw, body_yaw, screen[8], camera[3];
    int why, instance, held, kind, stance, learned, flags;
    LONG frame, stale, body_age;
} sight_row;

typedef struct {
    LONG frame;
    double axes[9], place[3], scale;
} placed_model;

typedef struct {
    double at;
    LONG frame;
    float dt, share, raw_deg, raw_rear[3], shown_deg, shown_rear[3], turn_deg, move_mm, speed, view_s, pitch, sway_move[3], sway_turn[3], chest;
    int stance, aim;
} pin_row;

typedef struct {
    void *skeleton;
    UINT32 joints;
    int ok;
    unsigned char level[MAX_SHIFTED_JOINTS];
} pin_table;

static rounding cameras[CAMERA_RING], attachments[ATTACHMENT_RING];
static skin_shift shifts[SHIFTS];
static volatile LONG camera_head, attachment_head, camera_frame;
static volatile float camera_at[3];
static volatile UINT32 body_at[3];
static volatile LONG camera_adds, kept_adds, near_draws, placed_draws, body_draws, camera_hits, skins, shifted[3];
static volatile LONG pass_camera_offset = -1, pass_scans, pass_same, pass_differ;
static volatile UINT32 joints_seen[3], bones_seen[3];
static volatile float camera_lost[3], gun_lost[3], shift_seen[3][3], unit_seen, moved_error, pass_gap, miss_gap = -1;
static void (*skinning_computation)(unsigned char *context);
static sight_bin sight_bins[SIGHT_KINDS][SIGHT_BINS];
static double sight_eased[6], sight_last_at, sight_last_view[3], sight_last_camera[3], sight_last_body[3], sight_last_sample[3];
static double sight_follow, sight_follow_at, sight_held[6];
static double sight_stance_changed_at = -1e9;
static int sight_crouched;
static LONG sight_settled, sight_traced, follow_tried = -1, follow_frames[2] = {-1, -1}, follow_picked_frame = -1, follow_picked = -1;
static float follow_duals[2][8], carry_duals[2][8];
static int carry_leveled[2];
static volatile LONG pin_wanted, pin_ready, pin_frames, pin_level_skins, pin_rigid_skins, pin_other_vtables, pin_refused;
static void *pin_character_vtable, *pin_parent_fn, *pin_name_fn;
static double pin_weight, pin_weight_at, pin_share_value, pin_at, pin_shown[4], pin_mid[4], pin_rear[3], pin_mid_rear[3];
static double pin_last_right[3], pin_last_ahead[3], pin_last_up[3], pin_last_camera[3], pin_raw_peak, pin_turn_peak, pin_move_peak;
static double sway_at[3], sway_rate[3], sway_camera[3], sway_look[3], sway_look_rate[3], sway_aim;
static volatile double hit_at = -1e12, hit_size, hit_side, hit_down_from, hit_side_from;
static volatile double pin_ease_until = -1e12;
static LONG pin_share_frame = -1, pin_step_frame = -1, pin_tables_next, pin_rowed;
static int pin_live, pin_paths_seen;
static char pin_paths[PIN_PATHS][MAX_PATH];
static pin_table pin_tables[PIN_TABLES];
static pin_row pin_rows[PIN_ROWS];
static FILE *pin_file;
static const float LEVEL_SHARES[PIN_LEVELS] = {0, 0.15f, 0.25f, 0.35f, 0.5f, 0.6f, 0.85f, 1, 1, 1, 1};
static volatile float pin_chest = 1;
static const struct { const char *name; unsigned char level; } JOINT_LEVELS[] = {
    {"Hips", 0}, {"Spine", 1}, {"Spine1", 3}, {"Spine2", 5}, {"Spine3", 6}, {"Spine4", 7}, {"Neck", 4}, {"Neck1", 2}, {"Head", 0},
    {"LeftArm", PIN_LEVEL_ARM}, {"RightArm", PIN_LEVEL_ARM}, {"LeftForeArm", PIN_LEVEL_ELBOW}, {"RightForeArm", PIN_LEVEL_ELBOW}, {"LeftHand", 10}, {"RightHand", 10},
};
static placed_model gun_placed[2] = {{-1}, {-1}}, body_placed[2] = {{-1}, {-1}};
static sight_trace sight_traces[SIGHT_TRACE_FRAMES];
static sight_row sight_rows[SIGHT_ROWS];
static LONG sight_rowed;
static volatile LONG stale_total;
static FILE *row_file;
extern char game_root[MAX_PATH];
static LONG sight_active, sight_steady, sight_frame = -1, sight_none = -1;
static volatile LONG sight_lock, sight_frames, sight_learned, sight_skins, sight_kind, sight_stance;
static volatile LONG sight_off, sight_shooting, sight_turning, sight_swinging, sight_mounted, sight_followed, sight_walking, sight_flips, sight_stale;
static volatile LONG gun_lost_skins, gun_bare_skins, gun_rescued_skins, shift_full;
static volatile LONG follow_same, follow_late, first_gun, first_body, body_waited, body_gave_up, body_on_gun_thread, body_refixed;
static volatile DWORD gun_thread;
static double body_wait_peak, saddle_kick, saddle_kick_from, saddle_last_shot_ms = 1e12, saddle_reach, saddle_turned_at = -1e12;
static double saddle_cant, saddle_depth;
static int saddle_steady, raise_live;
static double raise_turn[4], raise_rear[3], raise_at;
static LONG sight_reports;
static void *volatile sight_instance;
static volatile float sight_joint[8] = {0, 0, 0, 1, 0, 0, 0, 0};
static float sight_cached[8];
static double sight_sum[8], sight_low[2] = {1e9, 1e9}, sight_high[2] = {-1e9, -1e9}, sight_fixed_peak, sight_learned_peak, sight_pitch, sight_turn, sight_move, sight_scale;
static float sight_view[4];

static double clock_seconds(void) {
    LARGE_INTEGER now, rate;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&rate);
    return (double)now.QuadPart / (double)rate.QuadPart;
}
static double (*sight_clock)(void) = clock_seconds;

static UINT32 bits(float value) {
    UINT32 out;
    memcpy(&out, &value, sizeof(out));
    return out;
}

static void keep(rounding *ring, volatile LONG *head, LONG size, const float *sum, const float *a, const float *b) {
    rounding *slot = &ring[InterlockedIncrement(head) & (size - 1)];
    LONG seq = slot->seq;
    if ((seq & 1) || InterlockedCompareExchange(&slot->seq, seq + 1, seq) != seq) return;
    for (int i = 0; i < 3; i++) {
        slot->key[i] = bits(sum[i]);
        slot->lost[i] = (float)((double)a[i] + (double)b[i] - (double)sum[i]);
    }
    InterlockedExchange(&slot->seq, seq + 2);
}

static int lost_from(rounding *ring, volatile LONG *head, LONG size, const float *at, float *lost) {
    UINT32 key[3] = {bits(at[0]), bits(at[1]), bits(at[2])};
    LONG newest = *head;
    for (LONG i = 0; i < size; i++) {
        rounding *slot = &ring[(newest - i) & (size - 1)];
        LONG seq = slot->seq;
        if (!seq || (seq & 1) || slot->key[0] != key[0] || slot->key[1] != key[1] || slot->key[2] != key[2]) continue;
        float found[3] = {slot->lost[0], slot->lost[1], slot->lost[2]};
        if (slot->seq != seq) continue;
        memcpy(lost, found, sizeof(found));
        return 1;
    }
    return 0;
}

static void camera_add(const float *a, const float *b, float *out) {
    for (int i = 0; i < 3; i++) {
        out[i] = a[i] + b[i];
        camera_at[i] = out[i];
        body_at[i] = bits(a[i]);
    }
    keep(cameras, &camera_head, CAMERA_RING, out, a, b);
    InterlockedIncrement(&camera_adds);
    InterlockedIncrement(&camera_frame);
}

static void attachment_add(const float *a, const float *b, float *out) {
    for (int i = 0; i < 3; i++) out[i] = a[i] + b[i];
    float x = out[0] - camera_at[0], y = out[1] - camera_at[1], z = out[2] - camera_at[2];
    if (x * x + y * y + z * z > KEEP_RADIUS_SQUARED) return;
    keep(attachments, &attachment_head, ATTACHMENT_RING, out, a, b);
    InterlockedIncrement(&kept_adds);
}

static int to_local(const float *m, const double *world, float *local) {
    double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], k = m[10];
    double det = a * (e * k - f * h) - b * (d * k - f * g) + c * (d * h - e * g);
    if (det > -1e-6 && det < 1e-6) return 0;
    double inverse[9] = {e * k - f * h, c * h - b * k, b * f - c * e,
                         f * g - d * k, a * k - c * g, c * d - a * f,
                         d * h - e * g, b * g - a * h, a * e - b * d};
    for (int i = 0; i < 3; i++)
        local[i] = (float)((inverse[3 * i] * world[0] + inverse[3 * i + 1] * world[1] + inverse[3 * i + 2] * world[2]) / det);
    return 1;
}

static skin_shift *shift_for(const void *instance, int claim) {
    ULONG_PTR home = ((ULONG_PTR)instance * 0x9e3779b97f4a7c15ull) >> 58;
    LONG frame = camera_frame;
    skin_shift *open = NULL;
    for (int i = 0; i < SHIFT_PROBE; i++) {
        skin_shift *entry = &shifts[(home + i) % SHIFTS];
        if (entry->instance == instance) return entry;
        if (claim && !open && (!entry->seq || frame - entry->frame > SHIFT_FRAMES)) open = entry;
    }
    return open;
}

static void store_shift(void *instance, const float *local, LONG kind, const float *sight) {
    skin_shift *entry = shift_for(instance, 1);
    if (!entry) {
        InterlockedIncrement(&shift_full);
        return;
    }
    LONG frame = camera_frame, seq = entry->seq;
    if (seq && !(seq & 1) && entry->instance == instance && entry->frame == frame && (entry->sighted || !sight)) return;
    if ((seq & 1) || InterlockedCompareExchange(&entry->seq, seq + 1, seq) != seq) return;
    entry->instance = instance;
    for (int i = 0; i < 3; i++) entry->shift[i] = local[i];
    for (int i = 0; i < 8; i++) entry->sight[i] = sight ? sight[i] : 0;
    entry->sighted = sight != NULL;
    entry->frame = frame;
    entry->kind = kind;
    InterlockedExchange(&entry->seq, seq + 2);
}

static int shift_of(const void *instance, float *shift, LONG *kind, float *sight, LONG *sighted, LONG *age) {
    skin_shift *entry = shift_for(instance, 0);
    if (!entry) return 0;
    for (int tries = 0; tries < SHIFT_READ_TRIES; tries++) {
        LONG seq = entry->seq;
        if (!seq) return 0;
        if (seq & 1) {
            YieldProcessor();
            continue;
        }
        *age = camera_frame - entry->frame;
        if (entry->instance != instance || *age > SHIFT_FRAMES) return 0;
        for (int i = 0; i < 3; i++) shift[i] = entry->shift[i];
        for (int i = 0; i < 8; i++) sight[i] = entry->sight[i];
        *kind = entry->kind;
        *sighted = entry->sighted;
        if (entry->seq == seq) return 1;
    }
    return 0;
}

static int readable(const void *start, SIZE_T length) {
    const unsigned char *at = start, *end = at + length;
    while (at < end) {
        MEMORY_BASIC_INFORMATION info;
        if (!VirtualQuery(at, &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                              PAGE_EXECUTE_WRITECOPY)))
            return 0;
        at = (const unsigned char *)info.BaseAddress + info.RegionSize;
    }
    return 1;
}

static const float *pass_camera_at(const unsigned char *pass, LONG offset, const float *camera) {
    if (!readable(pass + offset, sizeof(void *))) return NULL;
    const float *matrix = *(const float *const *)(pass + offset);
    if ((ULONG_PTR)matrix < 0x10000 || ((ULONG_PTR)matrix & 3) || !readable(matrix, 16 * sizeof(float))) return NULL;
    float x = matrix[3] - camera[0], y = matrix[7] - camera[1], z = matrix[11] - camera[2];
    if (x * x + y * y + z * z > PASS_CAMERA_RADIUS_SQUARED) return NULL;
    for (int row = 0; row < 3; row++) {
        float length = matrix[4 * row] * matrix[4 * row] + matrix[4 * row + 1] * matrix[4 * row + 1] + matrix[4 * row + 2] * matrix[4 * row + 2];
        if (length < 0.99f || length > 1.01f) return NULL;
    }
    return matrix;
}

static const float *check_pass_camera(const unsigned char *pass, const float *camera) {
    LONG offset = pass_camera_offset;
    if (offset < 0) {
        if (InterlockedIncrement(&pass_scans) > PASS_SCAN_LIMIT) return NULL;
        for (LONG at = 0; at < PASS_SCAN_BYTES && offset < 0; at += sizeof(void *))
            if (pass_camera_at(pass, at, camera)) offset = at;
        if (offset < 0) return NULL;
        pass_camera_offset = offset;
    }
    const float *matrix = pass_camera_at(pass, offset, camera);
    if (!matrix) return NULL;
    if (bits(matrix[3]) == bits(camera[0]) && bits(matrix[7]) == bits(camera[1]) && bits(matrix[11]) == bits(camera[2])) {
        InterlockedIncrement(&pass_same);
        return matrix;
    }
    InterlockedIncrement(&pass_differ);
    float x = matrix[3] - camera[0], y = matrix[7] - camera[1], z = matrix[11] - camera[2];
    float gap = sqrtf(x * x + y * y + z * z);
    if (gap > pass_gap) pass_gap = gap;
    return NULL;
}

static void note_miss(const float *at) {
    float nearest = -1;
    for (LONG i = 0; i < ATTACHMENT_RING; i++) {
        rounding *slot = &attachments[i];
        if (!slot->seq) continue;
        float key[3];
        for (int j = 0; j < 3; j++) {
            UINT32 b = slot->key[j];
            memcpy(&key[j], &b, sizeof(float));
        }
        float x = key[0] - at[0], y = key[1] - at[1], z = key[2] - at[2];
        float gap = sqrtf(x * x + y * y + z * z);
        if (nearest < 0 || gap < nearest) nearest = gap;
    }
    if (nearest >= 0 && (miss_gap < 0 || nearest < miss_gap)) miss_gap = nearest;
}

static void translation_of(const float *q, float *t);

static double dot3(const double *a, const double *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void cross3(const double *a, const double *b, double *out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static void quat_of(const double *turn, double *q) {
    double angle = sqrt(dot3(turn, turn)), s = angle > 1e-12 ? sin(angle / 2) / angle : 0.5;
    for (int i = 0; i < 3; i++) q[i] = turn[i] * s;
    q[3] = cos(angle / 2);
}

static void quat_times(const double *a, const double *b, double *out) {
    out[0] = a[3] * b[0] + b[3] * a[0] + a[1] * b[2] - a[2] * b[1];
    out[1] = a[3] * b[1] + b[3] * a[1] + a[2] * b[0] - a[0] * b[2];
    out[2] = a[3] * b[2] + b[3] * a[2] + a[0] * b[1] - a[1] * b[0];
    out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

static void rotate(const double *q, const double *v, double *out) {
    double t[3], u[3];
    cross3(q, v, t);
    for (int i = 0; i < 3; i++) t[i] *= 2;
    cross3(q, t, u);
    for (int i = 0; i < 3; i++) out[i] = v[i] + q[3] * t[i] + u[i];
}

static void dual_apply(const float *dual, const double *p, double *out) {
    double q[4] = {dual[0], dual[1], dual[2], dual[3]};
    float moved[3];
    translation_of(dual, moved);
    rotate(q, p, out);
    for (int i = 0; i < 3; i++) out[i] += moved[i];
}

static void dual_of(const double *q, const double *t, float *out) {
    double pure[4] = {t[0], t[1], t[2], 0}, half[4];
    quat_times(pure, q, half);
    for (int i = 0; i < 4; i++) {
        out[i] = (float)q[i];
        out[4 + i] = (float)(0.5 * half[i]);
    }
}

static void dual_times(const float *a, float *b) {
    double an[4], ad[4], bn[4], bd[4], n[4], left[4], right[4];
    for (int i = 0; i < 4; i++) {
        an[i] = a[i];
        ad[i] = a[4 + i];
        bn[i] = b[i];
        bd[i] = b[4 + i];
    }
    quat_times(an, bn, n);
    quat_times(an, bd, left);
    quat_times(ad, bn, right);
    for (int i = 0; i < 4; i++) {
        b[i] = (float)n[i];
        b[4 + i] = (float)(left[i] + right[i]);
    }
}

static void on_screen(const double *p, const double *right, const double *ahead, const double *up, double *out) {
    double depth = dot3(p, ahead);
    out[0] = atan2(dot3(p, right), depth) * 1000;
    out[1] = atan2(dot3(p, up), depth) * 1000;
}

static void learn_bin(sight_bin *bin, double share, const double *value) {
    if (share <= 0) return;
    bin->weight = fmin(bin->weight + share, SIGHT_LEARN_CAP);
    for (int i = 0; i < 6; i++) bin->value[i] += share / bin->weight * (value[i] - bin->value[i]);
}

static int bin_value(int kind, double at, double *value) {
    for (int k = 0; k < SIGHT_KINDS; k++) {
        const sight_bin *bins = sight_bins[kind ^ k];
        int low = -1, high = -1;
        for (int i = (int)at; i >= 0 && low < 0; i--)
            if (bins[i].weight > 0) low = i;
        for (int i = (int)ceil(at); i < SIGHT_BINS && high < 0; i++)
            if (bins[i].weight > 0) high = i;
        if (low < 0 && high < 0) continue;
        if (low < 0) low = high;
        if (high < 0) high = low;
        double share = high > low ? (at - low) / (high - low) : 0;
        for (int i = 0; i < 6; i++) value[i] = bins[low].value[i] + share * (bins[high].value[i] - bins[low].value[i]);
        return 1;
    }
    return 0;
}

static void fixed_sights(const double *rear, const double *front, const double *turn, const double *move, double *fixed_rear,
                         double *fixed_front) {
    double q[4], offset[3], spun[3];
    quat_of(turn, q);
    for (int i = 0; i < 3; i++) offset[i] = front[i] - rear[i];
    rotate(q, offset, spun);
    for (int i = 0; i < 3; i++) {
        fixed_rear[i] = rear[i] + move[i];
        fixed_front[i] = rear[i] + spun[i] + move[i];
    }
}

static double front_miss(const double *rear, const double *front, const double *turn, const double *move,
                         const double *right, const double *ahead, const double *up) {
    double fixed_rear[3], fixed_front[3], screen[2];
    fixed_sights(rear, front, turn, move, fixed_rear, fixed_front);
    on_screen(fixed_front, right, ahead, up, screen);
    return sqrt(screen[0] * screen[0] + screen[1] * screen[1]);
}

static void sight_world(const double *value, double weight, const double *right, const double *ahead, const double *up,
                        double *turn, double *move) {
    for (int i = 0; i < 3; i++) {
        turn[i] = weight * (value[0] * right[i] + value[1] * ahead[i] + value[2] * up[i]);
        move[i] = weight * (value[3] * right[i] + value[4] * ahead[i] + value[5] * up[i]);
    }
}

static void note_sight(const double *rear, const double *front, const double *turn, const double *move,
                       const double *right, const double *ahead, const double *up, int learned, double *screen) {
    double fixed_rear[3], fixed_front[3];
    fixed_sights(rear, front, turn, move, fixed_rear, fixed_front);
    on_screen(rear, right, ahead, up, screen);
    on_screen(front, right, ahead, up, screen + 2);
    on_screen(fixed_rear, right, ahead, up, screen + 4);
    on_screen(fixed_front, right, ahead, up, screen + 6);
    sight_turn = sqrt(dot3(turn, turn)) * DEGREES;
    sight_move = sqrt(dot3(move, move)) * 1000;
    InterlockedIncrement(&sight_frames);
    if (!learned) return;
    for (int i = 0; i < 8; i++) sight_sum[i] += screen[i];
    for (int i = 0; i < 2; i++) {
        sight_low[i] = fmin(sight_low[i], screen[6 + i]);
        sight_high[i] = fmax(sight_high[i], screen[6 + i]);
    }
}

static double heading(double x, double y) {
    return atan2(y, x) * DEGREES;
}

static void add_row(const sight_row *row) {
    if (sight_rowed < SIGHT_ROWS) sight_rows[sight_rowed++] = *row;
}

static void keep_row(sight_row *row, const double *ahead, LONG frame) {
    const placed_model *body = body_placed[frame & 1].frame == frame ? &body_placed[frame & 1] : &body_placed[(frame - 1) & 1];
    row->view_yaw = (float)heading(ahead[0], ahead[1]);
    row->body_age = body->frame < 0 ? -1 : frame - body->frame;
    row->body_yaw = body->frame < 0 ? 0 : (float)heading(body->axes[1], body->axes[4]);
    add_row(row);
}

static void unsolved_row(sight_row *row, int why, const double *rear, const double *front, const double *right, const double *ahead,
                         const double *up, const void *instance) {
    if (instance != sight_instance || !row->held) return;
    double screen[4];
    on_screen(rear, right, ahead, up, screen);
    on_screen(front, right, ahead, up, screen + 2);
    for (int i = 0; i < 8; i++) row->screen[i] = (float)screen[i % 4];
    row->why = why;
    keep_row(row, ahead, row->frame);
}

static double similar_scale(const double *axes) {
    double det = axes[0] * (axes[4] * axes[8] - axes[5] * axes[7]) - axes[1] * (axes[3] * axes[8] - axes[5] * axes[6]) +
                 axes[2] * (axes[3] * axes[7] - axes[4] * axes[6]);
    if (det <= 0) return 0;
    double scale = cbrt(det);
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++)
            if (fabs(axes[a] * axes[b] + axes[3 + a] * axes[3 + b] + axes[6 + a] * axes[6 + b] - (a == b ? scale * scale : 0)) >
                SIGHT_SIMILAR * scale * scale)
                return 0;
    return scale;
}

static void model_dual(const double *axes, double scale, const double *center, const double *turn, const double *move, float *out) {
    double turn_model[3], move_model[3], q[4], spun[3], shift[3];
    for (int i = 0; i < 3; i++) {
        turn_model[i] = (axes[i] * turn[0] + axes[3 + i] * turn[1] + axes[6 + i] * turn[2]) / scale;
        move_model[i] = (axes[i] * move[0] + axes[3 + i] * move[1] + axes[6 + i] * move[2]) / (scale * scale);
    }
    quat_of(turn_model, q);
    rotate(q, center, spun);
    for (int i = 0; i < 3; i++) shift[i] = center[i] - spun[i] + move_model[i];
    dual_of(q, shift, out);
}

static double saddle_kicked(double since_shot) {
    if (since_shot < saddle_last_shot_ms) saddle_kick_from = saddle_kick;
    saddle_last_shot_ms = since_shot;
    double settle = since_shot - SADDLE_KICK_RISE_MS;
    saddle_kick = settle < 0 ? saddle_kick_from + (1 - saddle_kick_from) * since_shot / SADDLE_KICK_RISE_MS
                  : settle < SADDLE_KICK_FALL_MS ? 0.5 * (1 + cos(settle / SADDLE_KICK_FALL_MS * 180 / DEGREES))
                                                 : 0;
    return saddle_kick;
}

static void steady_saddle(double *applied, const double *gun, double dt, double depth, double reach, const double *right,
                          const double *ahead, const double *up) {
    double turn[3], q[4], grip[3], spun[3], twist[3], tq[4], total[4];
    for (int i = 0; i < 3; i++) {
        turn[i] = applied[0] * right[i] + applied[1] * ahead[i] + applied[2] * up[i];
        grip[i] = gun[3 * i + 1];
    }
    quat_of(turn, q);
    rotate(q, grip, spun);
    double cant = atan2(dot3(spun, right), dot3(spun, up));
    if (!saddle_steady) {
        saddle_cant = cant;
        saddle_depth = depth;
        saddle_steady = 1;
    }
    double ease = 1 - exp(-dt / SADDLE_STEADY_S);
    saddle_cant += ease * remainder(cant - saddle_cant, 2 * M_PI);
    saddle_depth += ease * (depth - saddle_depth);
    double untwist = sight_follow * remainder(saddle_cant - cant, 2 * M_PI);
    for (int i = 0; i < 3; i++) twist[i] = ahead[i] * untwist;
    quat_of(twist, tq);
    quat_times(tq, q, total);
    double sine = sqrt(total[0] * total[0] + total[1] * total[1] + total[2] * total[2]), angle = 2 * atan2(sine, total[3]);
    for (int i = 0; i < 3; i++) turn[i] = sine > 1e-12 ? total[i] / sine * angle : 0;
    applied[0] = dot3(turn, right);
    applied[1] = dot3(turn, ahead);
    applied[2] = dot3(turn, up);
    applied[4] += sight_follow * (saddle_depth - reach - applied[4]);
}

static void quat_of_axes(const double *m, double *q) {
    double t = m[0] + m[4] + m[8], s;
    if (t > 0) {
        s = 2 * sqrt(t + 1);
        q[0] = (m[7] - m[5]) / s, q[1] = (m[2] - m[6]) / s, q[2] = (m[3] - m[1]) / s, q[3] = s / 4;
    } else if (m[0] > m[4] && m[0] > m[8]) {
        s = 2 * sqrt(1 + m[0] - m[4] - m[8]);
        q[0] = s / 4, q[1] = (m[1] + m[3]) / s, q[2] = (m[2] + m[6]) / s, q[3] = (m[7] - m[5]) / s;
    } else if (m[4] > m[8]) {
        s = 2 * sqrt(1 + m[4] - m[0] - m[8]);
        q[0] = (m[1] + m[3]) / s, q[1] = s / 4, q[2] = (m[5] + m[7]) / s, q[3] = (m[2] - m[6]) / s;
    } else {
        s = 2 * sqrt(1 + m[8] - m[0] - m[4]);
        q[0] = (m[2] + m[6]) / s, q[1] = (m[5] + m[7]) / s, q[2] = s / 4, q[3] = (m[3] - m[1]) / s;
    }
}

static double turn_between(const double *to, const double *from, double *turn) {
    double back[4] = {-from[0], -from[1], -from[2], from[3]}, q[4];
    quat_times(to, back, q);
    if (q[3] < 0)
        for (int i = 0; i < 4; i++) q[i] = -q[i];
    double sine = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]), angle = 2 * atan2(sine, q[3]);
    for (int i = 0; i < 3; i++) turn[i] = sine > 1e-12 ? q[i] / sine * angle : 0;
    return angle;
}

static int raise_step(const double *goal, const double *gun, double scale, const double *rear, double now, const double *right,
                      const double *ahead, const double *up, double *shown) {
    const double *basis[3] = {right, ahead, up};
    double axes[9], raw[4], q[4], aim[4], spot[3], at[3], left[3], step[3];
    for (int a = 0; a < 3; a++) {
        at[a] = dot3(rear, basis[a]);
        spot[a] = at[a] + goal[3 + a];
        for (int k = 0; k < 3; k++) axes[3 * a + k] = (gun[k] * basis[a][0] + gun[3 + k] * basis[a][1] + gun[6 + k] * basis[a][2]) / scale;
    }
    quat_of_axes(axes, raw);
    quat_of(goal, q);
    quat_times(q, raw, aim);
    double dt = raise_live ? now - raise_at : 0;
    raise_at = now;
    if (!raise_live) {
        memcpy(raise_turn, aim, sizeof(aim));
        memcpy(raise_rear, spot, sizeof(spot));
        raise_live = 1;
    }
    double angle = turn_between(aim, raise_turn, left), gone[3], apart;
    for (int i = 0; i < 3; i++) gone[i] = spot[i] - raise_rear[i];
    apart = sqrt(dot3(gone, gone));
    double ease = 1 - exp(-dt * 1000 / m4_saddle_ease);
    double spin = angle > 1e-12 ? fmin(ease, m4_saddle_rate / DEGREES * dt / angle) : 1;
    double slide = apart > 1e-12 ? fmin(ease, SADDLE_SLIDE_M_S * dt / apart) : 1;
    for (int i = 0; i < 3; i++) {
        step[i] = left[i] * spin;
        raise_rear[i] += slide * gone[i];
    }
    quat_of(step, q);
    quat_times(q, raise_turn, aim);
    double size = sqrt(aim[0] * aim[0] + aim[1] * aim[1] + aim[2] * aim[2] + aim[3] * aim[3]);
    for (int i = 0; i < 4; i++) raise_turn[i] = aim[i] / size;
    angle = turn_between(raise_turn, raw, shown);
    for (int i = 0; i < 3; i++) shown[3 + i] = raise_rear[i] - at[i];
    return angle * DEGREES > SADDLE_REST_DEG || sqrt(dot3(shown + 3, shown + 3)) > SADDLE_REST_M;
}

static void sight_commit(const double *gun, double scale, const double *rear, const double *place, const double *turn_world,
                         const double *move_world, const double *eased_turn, const double *eased_move, int leveled, LONG frame, float *out) {
    model_dual(gun, scale, rear, eased_turn, eased_move, out);
    float undo[8] = {-out[0], -out[1], -out[2], out[3], -out[4], -out[5], -out[6], out[7]};
    model_dual(gun, scale, rear, turn_world, move_world, carry_duals[frame & 1]);
    dual_times(carry_duals[frame & 1], undo);
    memcpy(follow_duals[frame & 1], undo, sizeof(undo));
    follow_frames[frame & 1] = frame;
    carry_leveled[frame & 1] = leveled;
    placed_model *placed = &gun_placed[frame & 1];
    memcpy(placed->axes, gun, sizeof(placed->axes));
    memcpy(placed->place, place, sizeof(placed->place));
    placed->scale = scale;
    placed->frame = follow_tried = frame;
}

static float level_share(int level) {
    float chest = pin_chest;
    if (level == PIN_LEVEL_ARM) return 1 - (1 - chest) * PIN_ARM_PULL;
    if (level == PIN_LEVEL_ELBOW) return 1 - (1 - chest) * PIN_ELBOW_PULL;
    return level < PIN_LEVEL_ARM ? LEVEL_SHARES[level] * chest : LEVEL_SHARES[level];
}

static void pin_chest_step(double carried, double elapsed) {
    double limit = m4_pin_body / 1000.0, target = pin_wanted == PIN_SADDLE ? 0 : carried > limit ? limit / carried : 1;
    pin_chest += (float)((target - pin_chest) * (1 - exp(-fmin(elapsed, PIN_DT_CAP_S) / PIN_BODY_TAU_S)));
}

void m4_pin_signal(int on) {
    InterlockedExchange(&pin_wanted, on);
}

void m4_pin_ease(void) {
    pin_ease_until = sight_clock() + PIN_EASE_WINDOW_S;
}

static void hit_amounts(double now, double *down, double *side) {
    double t = now - hit_at;
    *down = *side = 0;
    if (t < 0 || t >= HIT_LAST_S) return;
    double shape = t / HIT_TAU_S * exp(1 - t / HIT_TAU_S), fade = exp(-t / HIT_TAU_S);
    *down = hit_size * shape + hit_down_from * fade;
    *side = hit_size * hit_side * shape + hit_side_from * fade;
}

void m4_hit_signal(double lost) {
    double now = sight_clock(), down, side;
    hit_amounts(now, &down, &side);
    hit_down_from = down;
    hit_side_from = side;
    hit_size = HIT_SMALLEST + (1 - HIT_SMALLEST) * fmin(1, lost / HIT_FULL_LOSS);
    hit_side = ((LONG)(now * 1000) >> 3) & 1 ? 1 : -1;
    hit_at = now;
}

static void hit_flinch(double now, double scale, double *value) {
    double down, side, gain = scale * m4_hit_gain / 100.0;
    hit_amounts(now, &down, &side);
    value[0] -= gain * down * HIT_PITCH_DEG / DEGREES;
    value[1] += gain * side * HIT_ROLL_DEG / DEGREES;
    value[2] += gain * side * HIT_YAW_DEG / DEGREES;
    value[3] += gain * side * HIT_SIDE_M;
    value[4] -= gain * down * HIT_BACK_M;
    value[5] -= gain * down * HIT_DROP_M;
}

static double pin_share(double now, int stance, LONG frame) {
    if (pin_share_frame == frame) return pin_share_value;
    pin_share_frame = frame;
    int wanted = pin_wanted && (m4_precision & PRECISION_PIN) && pin_ready && !((pin_wanted == PIN_SADDLE || (stance & STANCE_MOUNTED)) && m4_aim_held());
    double dt = fmin(now - pin_weight_at, PIN_DT_CAP_S);
    pin_weight_at = now;
    pin_weight = wanted ? fmin(1, pin_weight + dt / (now < pin_ease_until ? PIN_EASE_S : PIN_RISE_S)) : fmax(0, pin_weight - dt / PIN_FALL_S);
    if (pin_weight <= 0) pin_live = 0;
    pin_share_value = pin_weight * pin_weight * (3 - 2 * pin_weight);
    return pin_share_value;
}

static int has_word(const char *text, const char *word) {
    size_t length = strlen(word);
    for (; *text; text++)
        if (!_strnicmp(text, word, length)) return 1;
    return 0;
}

static void pin_note_path(const char *path, int gun) {
    for (int i = 0; i < pin_paths_seen; i++)
        if (!strcmp(pin_paths[i], path)) return;
    if (pin_paths_seen >= PIN_PATHS) return;
    snprintf(pin_paths[pin_paths_seen++], MAX_PATH, "%s", path);
    char line[MAX_PATH + 64];
    snprintf(line, sizeof(line), "pin: near character %s %s", path, gun ? "is the gun" : "stays as posed");
    note(line);
}

static int pin_gun(void *instance) {
    if (!instance) return 0;
    void **slots = *(void ***)instance;
    if (slots != pin_character_vtable) {
        InterlockedIncrement(&pin_other_vtables);
        return 0;
    }
    const char *path = ((const char *(*)(void *))slots[CHARACTER_PATH_SLOT])(instance);
    if (!path) return 0;
    int gun = has_word(path, "m4carbine") || has_word(path, "m4_runtime");
    pin_note_path(path, gun);
    return gun;
}

static void pin_goal(const float *joint, const double *rear, const double *front, double *goal) {
    double low[3], high[3], d[3], line[3], upward[3], side[3];
    dual_apply(joint, SIGHT_REAR, low);
    dual_apply(joint, DOT_REAR, high);
    for (int i = 0; i < 3; i++) {
        d[i] = high[i] - low[i];
        line[i] = front[i] - rear[i];
    }
    double size = sqrt(dot3(line, line));
    for (int i = 0; i < 3; i++) line[i] /= size;
    double along = dot3(d, line);
    for (int i = 0; i < 3; i++) upward[i] = d[i] - along * line[i];
    size = sqrt(dot3(upward, upward));
    for (int i = 0; i < 3; i++) upward[i] /= size;
    cross3(line, upward, side);
    double axes[9] = {side[0], side[1], side[2], line[0], line[1], line[2], upward[0], upward[1], upward[2]};
    quat_of_axes(axes, goal);
}

static void chase_quat(double *q, const double *target, double ease) {
    double turn[3], part[4], next[4];
    turn_between(target, q, turn);
    for (int i = 0; i < 3; i++) turn[i] *= ease;
    quat_of(turn, part);
    quat_times(part, q, next);
    double size = sqrt(next[0] * next[0] + next[1] * next[1] + next[2] * next[2] + next[3] * next[3]);
    for (int i = 0; i < 4; i++) q[i] = next[i] / size;
}

static void sway_reset(int aimed) {
    memset(sway_at, 0, sizeof(sway_at));
    memset(sway_rate, 0, sizeof(sway_rate));
    memset(sway_camera, 0, sizeof(sway_camera));
    memset(sway_look, 0, sizeof(sway_look));
    memset(sway_look_rate, 0, sizeof(sway_look_rate));
    sway_aim = aimed;
}

static void sway_step(const float *camera, const double *right, const double *ahead, const double *up, double elapsed, int aimed) {
    if (elapsed <= 0) return;
    const double *axes[3] = {right, ahead, up}, *was[3] = {pin_last_right, pin_last_ahead, pin_last_up};
    double dt = fmin(elapsed, PIN_DT_CAP_S), moved[3], accel[3], spin[3] = {0, 0, 0}, goal[3];
    for (int i = 0; i < 3; i++) moved[i] = ((double)camera[i] - pin_last_camera[i]) / elapsed;
    int jumped = elapsed > PIN_DT_CAP_S || sqrt(dot3(moved, moved)) > SWAY_TELEPORT_M_S;
    double follow = jumped ? 0 : 1 - exp(-dt / SWAY_CAMERA_TAU_S);
    for (int i = 0; i < 3; i++) {
        accel[i] = follow * (moved[i] - sway_camera[i]) / dt;
        sway_camera[i] += accel[i] * dt;
    }
    double hard = sqrt(dot3(accel, accel));
    if (hard > SWAY_ACCEL_CAP)
        for (int i = 0; i < 3; i++) accel[i] *= SWAY_ACCEL_CAP / hard;
    for (int a = 0; a < 3; a++) {
        double part[3];
        cross3(was[a], axes[a], part);
        for (int i = 0; i < 3; i++) spin[i] += 0.5 * part[i];
    }
    const double cap = SWAY_LOOK_CAP_DEG / DEGREES, lag = jumped ? 0 : m4_sway_look / 1000.0 / elapsed;
    for (int a = 0; a < 3; a++) goal[a] = fmax(-cap, fmin(cap, -lag * dot3(spin, axes[a])));
    double w = 2 * M_PI * m4_sway_hz / 100.0, gain = m4_sway_move / 100.0, lean = m4_sway_lean / 1000.0;
    int steps = (int)ceil(dt / SWAY_STEP_S);
    double h = dt / steps;
    for (int s = 0; s < steps; s++) {
        for (int i = 0; i < 3; i++) {
            sway_rate[i] += h * (w * w * (-lean * sway_camera[i] - sway_at[i]) - gain * accel[i] - 2 * SWAY_DAMPING * w * sway_rate[i]);
            sway_at[i] += h * sway_rate[i];
            sway_look_rate[i] += h * (w * w * (goal[i] - sway_look[i]) - 2 * SWAY_DAMPING * w * sway_look_rate[i]);
            sway_look[i] += h * sway_look_rate[i];
        }
    }
    sway_aim += (1 - exp(-dt / PIN_TAU_S)) * (aimed - sway_aim);
}

static double sway_shown(void) {
    return 1 + (m4_sway_aim / 100.0 - 1) * sway_aim;
}

static void sway_pose(const double *right, const double *ahead, const double *up, double *turn, double *move) {
    double size = sqrt(dot3(sway_at, sway_at)), shown = sway_shown();
    double keep = shown * (size > 0 ? SWAY_LIMIT_M * tanh(size / SWAY_LIMIT_M) / size : 1), tilt = m4_sway_tilt / DEGREES;
    move[0] = dot3(sway_at, right) * keep;
    move[1] = dot3(sway_at, ahead) * keep;
    move[2] = dot3(sway_at, up) * keep;
    turn[0] = tilt * (move[2] + SWAY_DIP * move[1]) + sway_look[0] * shown;
    turn[1] = -tilt * SWAY_ROLL * move[0] + (sway_look[1] + SWAY_LOOK_ROLL * sway_look[2]) * shown;
    turn[2] = -tilt * move[0] + sway_look[2] * shown;
}

static void pin_value(const double *gun, double scale, const float *joint, const double *rear, const double *front, const double *r,
                      const float *camera, double now, double share, int stance, LONG frame, const double *right, const double *ahead,
                      const double *up, double *value) {
    const double *basis[3] = {right, ahead, up};
    double axes[9], raw[4], goal[4], at[3], kicked[4], kick_turn[3] = {0, 0, 0}, kick_q[4], unused[3];
    for (int a = 0; a < 3; a++) {
        at[a] = dot3(r, basis[a]);
        for (int k = 0; k < 3; k++) axes[3 * a + k] = (gun[k] * basis[a][0] + gun[3 + k] * basis[a][1] + gun[6 + k] * basis[a][2]) / scale;
    }
    quat_of_axes(axes, raw);
    pin_goal(joint, rear, front, goal);
    int stepping = pin_step_frame != frame, aimed = m4_aim_held();
    double elapsed = pin_live ? now - pin_at : 0;
    if (stepping) {
        pin_step_frame = frame;
        pin_at = now;
        if (!pin_live) {
            memcpy(pin_shown, raw, sizeof(raw));
            memcpy(pin_mid, raw, sizeof(raw));
            memcpy(pin_rear, at, sizeof(at));
            memcpy(pin_mid_rear, at, sizeof(at));
            sway_reset(aimed);
            pin_chest = pin_wanted == PIN_SADDLE || m4_pin_body <= 0 ? 0 : 1;
            pin_live = 1;
        } else {
            const double spot[3] = {aimed ? 0 : m4_hip_right / 1000.0, (aimed ? m4_aim_depth : m4_hip_ahead) / 1000.0,
                                    aimed ? 0 : m4_hip_up / 1000.0};
            double ease = 1 - exp(-fmin(elapsed, PIN_DT_CAP_S) / PIN_TAU_S);
            chase_quat(pin_mid, goal, ease);
            chase_quat(pin_shown, pin_mid, ease);
            for (int i = 0; i < 3; i++) {
                pin_mid_rear[i] += ease * (spot[i] - pin_mid_rear[i]);
                pin_rear[i] += ease * (pin_mid_rear[i] - pin_rear[i]);
            }
            pin_chest_step(sqrt(pow(pin_rear[0] - at[0], 2) + pow(pin_rear[1] - at[1], 2) + pow(pin_rear[2] - at[2], 2)), elapsed);
            sway_step(camera, right, ahead, up, elapsed, aimed);
        }
        saddle_kicked(m4_since_shot_ms());
    }
    double sway_turn[3], sway_move[3], hit[6] = {0, 0, 0, 0, 0, 0};
    sway_pose(right, ahead, up, sway_turn, sway_move);
    hit_flinch(now, sway_shown(), hit);
    for (int i = 0; i < 3; i++) kick_turn[i] = sway_turn[i] + hit[i];
    kick_turn[0] += saddle_kick * m4_saddle_lift / 100.0 / DEGREES;
    quat_of(kick_turn, kick_q);
    quat_times(kick_q, pin_shown, kicked);
    double turned = turn_between(kicked, raw, value);
    for (int i = 0; i < 3; i++) value[3 + i] = pin_rear[i] + sway_move[i] + hit[3 + i] - at[i];
    value[4] -= saddle_kick * m4_saddle_push / 1000.0;
    if (!stepping) return;
    double raw_deg = turn_between(goal, raw, unused) * DEGREES, shown_deg = turn_between(goal, pin_shown, unused) * DEGREES;
    double moved = sqrt(dot3(value + 3, value + 3)), walked = 0, spun = 0;
    if (elapsed > 0) {
        double step[3], spin[3];
        for (int i = 0; i < 3; i++) step[i] = (double)camera[i] - pin_last_camera[i];
        cross3(ahead, pin_last_ahead, spin);
        walked = sqrt(dot3(step, step)) / elapsed;
        spun = atan2(sqrt(dot3(spin, spin)), dot3(ahead, pin_last_ahead)) * DEGREES / elapsed;
    }
    for (int i = 0; i < 3; i++) {
        pin_last_camera[i] = camera[i];
        pin_last_right[i] = right[i];
        pin_last_ahead[i] = ahead[i];
        pin_last_up[i] = up[i];
    }
    pin_raw_peak = fmax(pin_raw_peak, raw_deg);
    pin_turn_peak = fmax(pin_turn_peak, turned * DEGREES);
    pin_move_peak = fmax(pin_move_peak, moved * 1000);
    if (pin_rowed >= PIN_ROWS) return;
    pin_row *row = &pin_rows[pin_rowed++];
    *row = (pin_row){.at = now, .frame = frame, .dt = (float)(elapsed * 1000), .share = (float)share, .raw_deg = (float)raw_deg,
                     .shown_deg = (float)shown_deg, .turn_deg = (float)(turned * DEGREES), .move_mm = (float)(moved * 1000),
                     .speed = (float)walked, .view_s = (float)spun, .pitch = (float)(asin(fmax(-1, fmin(1, ahead[2]))) * DEGREES),
                     .stance = stance, .aim = aimed};
    for (int i = 0; i < 3; i++) {
        row->raw_rear[i] = (float)(at[i] * 1000);
        row->shown_rear[i] = (float)(pin_rear[i] * 1000);
        row->sway_move[i] = (float)(sway_move[i] * 1000);
        row->sway_turn[i] = (float)(sway_turn[i] * DEGREES);
        row->chest = pin_chest;
    }
}

static int pin_commit(const double *value, double share, const double *gun, double scale, const double *rear, const double *place,
                      const double *right, const double *ahead, const double *up, void *instance, LONG frame, float *out) {
    double turn[3], move[3];
    sight_world(value, share, right, ahead, up, turn, move);
    sight_commit(gun, scale, rear, place, turn, move, turn, move, 1, frame, out);
    sight_instance = instance;
    InterlockedIncrement(&pin_frames);
    return 1;
}

static double sight_roll(const float *joint, const double *rear, const double *front, const double *gun, const double *turn,
                         const double *right, const double *up) {
    double low[3], high[3], d[3], line[3], upward[3], world[3], q[4], spun[3];
    dual_apply(joint, SIGHT_REAR, low);
    dual_apply(joint, DOT_REAR, high);
    for (int i = 0; i < 3; i++) {
        d[i] = high[i] - low[i];
        line[i] = front[i] - rear[i];
    }
    double size = sqrt(dot3(line, line));
    for (int i = 0; i < 3; i++) line[i] /= size;
    double along = dot3(d, line), cap = SIGHT_ROLL_CAP_DEG / DEGREES;
    for (int i = 0; i < 3; i++) upward[i] = d[i] - along * line[i];
    for (int i = 0; i < 3; i++) world[i] = gun[3 * i] * upward[0] + gun[3 * i + 1] * upward[1] + gun[3 * i + 2] * upward[2];
    quat_of(turn, q);
    rotate(q, world, spun);
    return fmax(-cap, fmin(cap, -atan2(dot3(spun, right), dot3(spun, up))));
}

static void roll_on_line(double *applied, const double *right, const double *ahead, const double *up) {
    double turn[3], twist[3], q[4], tq[4], total[4];
    for (int i = 0; i < 3; i++) {
        turn[i] = applied[0] * right[i] + applied[2] * up[i];
        twist[i] = applied[1] * ahead[i];
    }
    quat_of(turn, q);
    quat_of(twist, tq);
    quat_times(tq, q, total);
    double sine = sqrt(total[0] * total[0] + total[1] * total[1] + total[2] * total[2]), angle = 2 * atan2(sine, total[3]);
    for (int i = 0; i < 3; i++) turn[i] = sine > 1e-12 ? total[i] / sine * angle : 0;
    applied[0] = dot3(turn, right);
    applied[1] = dot3(turn, ahead);
    applied[2] = dot3(turn, up);
}

static int sight_solved(const float *m, const float *view, const float *at, const float *camera, const double *world,
                        void *instance, LONG frame, float *out) {
    double gun[9] = {m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]};
    double right[3] = {view[0], view[4], view[8]}, ahead[3] = {view[1], view[5], view[9]}, up[3] = {view[2], view[6], view[10]};
    double scale = similar_scale(gun), now = sight_clock();
    int stance = m4_stance();
    if ((stance & STANCE_CROUCHED) != sight_crouched) sight_stance_changed_at = now;
    sight_crouched = stance & STANCE_CROUCHED;
    int stance_locked = !(stance & STANCE_MOUNTED) && now - sight_stance_changed_at < SIGHT_STANCE_LOCK_S;
    sight_row row = {.at = now, .frame = frame, .instance = (int)(((ULONG_PTR)instance >> 4) & 0xffff), .held = m4_aim_held(),
                     .stance = stance, .camera = {camera[0], camera[1], camera[2]}, .stale = stale_total};
    double share = pin_share(now, stance, frame), value[6];
    if (share > 0 && !pin_gun(instance)) return 0;
    if (instance == sight_instance) {
        follow_tried = frame;
        gun_thread = GetCurrentThreadId();
    }
    if (scale <= 0) return 0;
    float joint[8] = {0, 0, 0, 1, 0, 0, 0, 0};
    if (instance == sight_instance)
        for (int i = 0; i < 8; i++) joint[i] = sight_joint[i];
    double rear[3], front[3], r[3], f[3], place[3];
    dual_apply(joint, m4_sight_dot ? DOT_REAR : SIGHT_REAR, rear);
    dual_apply(joint, m4_sight_dot ? DOT_FRONT : SIGHT_FRONT, front);
    for (int i = 0; i < 3; i++) {
        double base = place[i] = (double)at[i] - (double)camera[i] + world[i];
        r[i] = base + gun[3 * i] * rear[0] + gun[3 * i + 1] * rear[1] + gun[3 * i + 2] * rear[2];
        f[i] = base + gun[3 * i] * front[0] + gun[3 * i + 1] * front[1] + gun[3 * i + 2] * front[2];
    }
    if (share > 0) pin_value(gun, scale, joint, rear, front, r, camera, now, share, stance, frame, right, ahead, up, value);
    if (share >= 1) {
        sight_active = sight_steady = 0;
        return pin_commit(value, share, gun, scale, rear, place, right, ahead, up, instance, frame, out);
    }
    double line[3] = {f[0] - r[0], f[1] - r[1], f[2] - r[2]}, length = sqrt(dot3(line, line)), reach = dot3(r, ahead);
    double dt = sight_active ? now - sight_last_at : 0, spin[3];
    cross3(ahead, sight_last_view, spin);
    double turned = atan2(sqrt(dot3(spin, spin)), dot3(ahead, sight_last_view)) * DEGREES;
    int saddle = (stance & STANCE_MOUNTED) && instance == sight_instance && sight_active && sight_settled && m4_aim_held();
    if (saddle && dt > 0 && turned / dt > SIGHT_FOLLOW_FULL_DEG_S) saddle_turned_at = now;
    int catching = saddle && now - saddle_turned_at < SADDLE_CATCH_S, beyond = reach < SIGHT_NEAR_M || reach > SIGHT_FAR_M;
    int raising = (m4_precision & PRECISION_FOLLOW) && m4_saddle_ease > 0 && m4_saddle_rate > 0 && (stance & STANCE_MOUNTED) && m4_aim_held();
    if (!raising) raise_live = 0;
    raising = raising && instance == sight_instance;
    if (length < 1e-6 || (beyond && !catching && !(raising && raise_live))) {
        unsolved_row(&row, ROW_OUT_OF_REACH, r, f, right, ahead, up, instance);
        return share > 0 ? pin_commit(value, share, gun, scale, rear, place, right, ahead, up, instance, frame, out) : 0;
    }
    double miss[3], axis[3];
    for (int i = 0; i < 3; i++) {
        line[i] /= length;
        miss[i] = ahead[i] * reach - r[i];
    }
    cross3(line, ahead, axis);
    double sine = sqrt(dot3(axis, axis)), angle = atan2(sine, dot3(line, ahead)) * DEGREES, gap = sqrt(dot3(miss, miss));
    double weight = gap > SIGHT_REACH_M || (beyond && !catching) ? 0 : fmin(1, fmax(0, (SIGHT_NONE_DEG - angle) / (SIGHT_NONE_DEG - SIGHT_FULL_DEG)));
    row.angle = (float)angle;
    row.gap = (float)(gap * 1000);
    if (catching && (beyond || weight < 1)) {
        for (int i = 0; i < 3; i++) miss[i] = ahead[i] * saddle_reach - r[i];
        gap = sqrt(dot3(miss, miss));
        weight = 1;
    } else if ((stance & STANCE_MOUNTED) && weight >= 1) {
        saddle_reach = reach;
    }
    double shown[6] = {0, 0, 0, 0, 0, 0};
    int stepped = weight <= 0 && raising && raise_live;
    int easing = stepped && raise_step(shown, gun, scale, r, now, right, ahead, up, shown);
    int saddled = weight > 0 && (stance & STANCE_MOUNTED), mounted = saddled || easing;
    if (mounted) InterlockedIncrement(&sight_mounted);
    if (weight <= 0 && !easing) {
        if (instance == sight_instance) sight_active = sight_steady = 0;
        sight_held[1] = 0;
        unsolved_row(&row, ROW_OFF_LINE, r, f, right, ahead, up, instance);
        return share > 0 ? pin_commit(value, share, gun, scale, rear, place, right, ahead, up, instance, frame, out) : 0;
    }
    double body[3];
    for (int i = 0; i < 3; i++) {
        UINT32 b = body_at[i];
        float value;
        memcpy(&value, &b, sizeof(value));
        body[i] = value;
    }
    double walked = hypot(body[0] - sight_last_body[0], body[1] - sight_last_body[1]);
    double carried = hypot((double)camera[0] - sight_last_camera[0], (double)camera[1] - sight_last_camera[1]);
    int walking = dt > 0 && walked > SIGHT_MOVING_M_S * dt, kind = walking | (stance & STANCE_CROUCHED) << 1;
    if (walking) InterlockedIncrement(&sight_walking);
    if (dt > 0 && carried > SIGHT_MOVING_M_S * dt && !walking) InterlockedIncrement(&sight_flips);
    double pitch = asin(fmax(-1, fmin(1, ahead[2]))) * DEGREES;
    double at_bin = fmin(SIGHT_BINS - 1, fmax(0, (pitch + 90) / SIGHT_BIN_DEG)), turn[3];
    for (int i = 0; i < 3; i++) turn[i] = sine > 1e-12 ? axis[i] / sine * angle / DEGREES : 0;
    int on_foot = !(stance & STANCE_MOUNTED);
    double sample[6] = {dot3(turn, right), on_foot ? sight_roll(joint, rear, front, gun, turn, right, up) : 0, dot3(turn, up), dot3(miss, right), dot3(miss, ahead), dot3(miss, up)};
    double drift[3] = {sample[0] - sight_last_sample[0], 0, sample[2] - sight_last_sample[2]};
    int turning = !(dt > 0 && turned < SIGHT_STEADY_DEG_S * dt);
    int swinging = !(dt > 0 && sqrt(dot3(drift, drift)) * DEGREES < SIGHT_GUN_DEG_S * dt);
    double since_shot = m4_since_shot_ms();
    int off = angle >= SIGHT_LEARN_DEG || gap >= SIGHT_LEARN_M || weight < 1, shooting = since_shot <= SIGHT_SHOT_QUIET_MS;
    sight_steady = sight_active && !turning && !swinging ? sight_steady + 1 : 0;
    int learned = sight_steady >= SIGHT_STEADY_FRAMES && !off && !shooting && !mounted;
    if (learned) {
        int low = (int)at_bin < SIGHT_BINS - 2 ? (int)at_bin : SIGHT_BINS - 2;
        learn_bin(&sight_bins[kind][low], low + 1 - at_bin, sample);
        learn_bin(&sight_bins[kind][low + 1], at_bin - low, sample);
        InterlockedIncrement(&sight_learned);
    }
    if (off) InterlockedIncrement(&sight_off);
    if (shooting) InterlockedIncrement(&sight_shooting);
    if (turning) InterlockedIncrement(&sight_turning);
    if (swinging) InterlockedIncrement(&sight_swinging);
    double target[6] = {0, 0, 0, 0, 0, 0}, ease = sight_active ? 1 - exp(-dt / SIGHT_EASE_S) : 1;
    if (!mounted) bin_value(kind, at_bin, target);
    target[1] = 0;
    for (int i = 0; i < 6; i++) sight_eased[i] += ease * (target[i] - sight_eased[i]);
    if (!sight_active || !m4_aim_held()) sight_settled = 0;
    else if (!off || stance_locked) sight_settled = 1;
    int following = (m4_precision & PRECISION_FOLLOW) && sight_settled;
    double fast = following && (mounted || stance_locked) ? 1
                  : following && dt > 0 && !shooting
                      ? fmin(1, fmax(0, (turned / dt - SIGHT_FOLLOW_FROM_DEG_S) / (SIGHT_FOLLOW_FULL_DEG_S - SIGHT_FOLLOW_FROM_DEG_S)))
                      : 0;
    if (!sight_settled) sight_follow = fast = 0;
    if (fast >= sight_follow) {
        sight_follow = fast;
        sight_follow_at = now;
    } else if (now - sight_follow_at > SIGHT_FOLLOW_HOLD_S) {
        sight_follow += (fast - sight_follow) * (1 - exp(-dt / SIGHT_FOLLOW_EASE_S));
    }
    int holding = (m4_precision & PRECISION_HOLD) && sight_settled;
    double pull = holding ? 1 - exp(-dt / SIGHT_HOLD_S) : 0, roll_pull = 1 - exp(-dt / SIGHT_ROLL_S), applied[6];
    for (int i = 0; i < 6; i++) {
        double raw = sample[i] - sight_eased[i];
        if (i == 1) {
            sight_held[1] += roll_pull * ((holding ? raw : 0) - sight_held[1]);
        } else {
            sight_held[i] = holding ? sight_held[i] + pull * (raw - sight_held[i]) : 0;
            sight_held[i] = raw > 0 ? fmax(0, fmin(sight_held[i], raw)) : fmin(0, fmax(sight_held[i], raw));
        }
        applied[i] = sight_eased[i] + sight_held[i] + sight_follow * (raw - sight_held[i]);
    }
    if (on_foot) roll_on_line(applied, right, ahead, up);
    if (saddled && sight_follow > 0) steady_saddle(applied, gun, dt, catching ? saddle_reach : reach, reach, right, ahead, up);
    else saddle_steady = 0;
    if (raising && !stepped) {
        for (int i = 0; i < 6; i++) shown[i] = weight * applied[i];
        raise_step(shown, gun, scale, r, now, right, ahead, up, shown);
        stepped = 1;
    }
    if (stepped)
        for (int i = 0; i < 6; i++) applied[i] = shown[i];
    if (saddled) {
        double kick = saddle_kicked(since_shot);
        applied[0] += kick * m4_saddle_lift / 100.0 / DEGREES;
        applied[4] -= kick * m4_saddle_push / 1000.0;
    }
    hit_flinch(now, m4_sway_aim / 100.0, applied);
    if ((learned || since_shot < SIGHT_TRACE_AFTER_SHOT_MS || (dt > 0 && (turned / dt > SIGHT_FOLLOW_FROM_DEG_S || sight_follow > 0))) &&
        sight_traced < SIGHT_TRACE_FRAMES)
        sight_traces[sight_traced++] = (sight_trace){now, (float)(dt * 1000), (float)(-dot3(spin, up) * DEGREES),
                                                     (float)(-dot3(spin, right) * DEGREES), (float)((sample[2] - sight_eased[2]) * DEGREES),
                                                     (float)((sample[0] - sight_eased[0]) * DEGREES), (float)(sample[2] * DEGREES),
                                                     (float)(sample[0] * DEGREES), (float)(gap * 1000), (float)weight,
                                                     (float)sight_follow, (float)fmin(99, since_shot / 1000), kind, learned};
    sight_active = 1;
    sight_last_at = now;
    for (int i = 0; i < 3; i++) {
        sight_last_view[i] = ahead[i];
        sight_last_camera[i] = camera[i];
        sight_last_body[i] = body[i];
        sight_last_sample[i] = sample[i];
    }
    double turn_world[3], move_world[3], eased_turn[3], eased_move[3];
    sight_world(applied, stepped ? 1 : weight, right, ahead, up, turn_world, move_world);
    sight_world(sight_eased, weight, right, ahead, up, eased_turn, eased_move);
    if (sight_follow > 0.5) {
        InterlockedIncrement(&sight_followed);
        sight_fixed_peak = fmax(sight_fixed_peak, front_miss(r, f, turn_world, move_world, right, ahead, up));
        sight_learned_peak = fmax(sight_learned_peak, front_miss(r, f, eased_turn, eased_move, right, ahead, up));
    }
    if (share > 0) {
        double pin_turn[3], pin_move[3];
        sight_world(value, share, right, ahead, up, pin_turn, pin_move);
        for (int i = 0; i < 3; i++) {
            eased_turn[i] = turn_world[i] = (1 - share) * turn_world[i] + pin_turn[i];
            eased_move[i] = move_world[i] = (1 - share) * move_world[i] + pin_move[i];
        }
    }
    sight_commit(gun, scale, rear, place, turn_world, move_world, eased_turn, eased_move, share > 0, frame, out);
    double screen[8];
    note_sight(r, f, turn_world, move_world, right, ahead, up, learned, screen);
    row.dt = (float)(dt * 1000);
    row.pitch = (float)pitch;
    row.turn_s = dt > 0 ? (float)(turned / dt) : 0;
    row.weight = (float)weight;
    row.follow = (float)sight_follow;
    row.pull = (float)(hypot(sight_held[0], sight_held[2]) * 1000);
    row.shot_s = (float)fmin(99, since_shot / 1000);
    row.kind = kind;
    row.learned = learned;
    row.flags = off | shooting << 1 | turning << 2 | swinging << 3 | sight_settled << 4 | walking << 5;
    for (int i = 0; i < 8; i++) row.screen[i] = (float)screen[i];
    keep_row(&row, ahead, frame);
    sight_pitch = pitch;
    sight_scale = scale;
    sight_kind = kind;
    sight_stance = stance;
    for (int i = 0; i < 4; i++) sight_view[i] = view[12 + i];
    sight_instance = instance;
    return 1;
}

static void refix_bodies(LONG frame);

static volatile double view_pitch, view_pitch_at = -1;

int m4_view_pitch(double *pitch) {
    double at = view_pitch_at;
    if (at < 0 || sight_clock() - at > VIEW_FRESH_S) return 0;
    *pitch = view_pitch;
    return 1;
}

static int sight_of(const float *m, const float *view, const float *at, const float *camera, const double *world,
                    void *instance, float *out) {
    view_pitch = asin(fmax(-1, fmin(1, view[9]))) * DEGREES;
    view_pitch_at = sight_clock();
    while (InterlockedCompareExchange(&sight_lock, 1, 0)) YieldProcessor();
    LONG frame = camera_frame;
    int ok = frame == sight_frame && instance == sight_instance;
    if (!ok && sight_solved(m, view, at, camera, world, instance, frame, sight_cached)) {
        sight_frame = frame;
        ok = 1;
    }
    if (ok) memcpy(out, sight_cached, sizeof(sight_cached));
    else if (instance == sight_instance) sight_none = frame;
    if (follow_tried == frame) refix_bodies(frame);
    InterlockedExchange(&sight_lock, 0);
    return ok;
}

static void note_body(const float *location, const float *at, const float *camera, const double *world) {
    placed_model placed = {camera_frame};
    for (int i = 0; i < 3; i++) {
        for (int k = 0; k < 3; k++) placed.axes[3 * i + k] = location[4 * i + k];
        placed.place[i] = (double)at[i] - (double)camera[i] + world[i];
    }
    placed.scale = similar_scale(placed.axes);
    if (placed.scale <= 0) return;
    while (InterlockedCompareExchange(&sight_lock, 1, 0)) YieldProcessor();
    body_placed[placed.frame & 1] = placed;
    InterlockedExchange(&sight_lock, 0);
}

static void carried_dual(const placed_model *gun, const placed_model *body, const float *dual, float *out) {
    double sine = sqrt((double)dual[0] * dual[0] + (double)dual[1] * dual[1] + (double)dual[2] * dual[2]);
    double angle = 2 * atan2(sine, dual[3]), spin[3], turn[3], move[3], offset[3], center[3];
    float shift[3];
    translation_of(dual, shift);
    for (int i = 0; i < 3; i++) spin[i] = sine > 1e-12 ? dual[i] / sine * angle : 2 * dual[i];
    for (int i = 0; i < 3; i++) {
        turn[i] = (gun->axes[3 * i] * spin[0] + gun->axes[3 * i + 1] * spin[1] + gun->axes[3 * i + 2] * spin[2]) / gun->scale;
        move[i] = gun->axes[3 * i] * shift[0] + gun->axes[3 * i + 1] * shift[1] + gun->axes[3 * i + 2] * shift[2];
        offset[i] = gun->place[i] - body->place[i];
    }
    for (int i = 0; i < 3; i++)
        center[i] = (body->axes[i] * offset[0] + body->axes[3 + i] * offset[1] + body->axes[6 + i] * offset[2]) / (body->scale * body->scale);
    model_dual(body->axes, body->scale, center, turn, move, out);
}

static void wait_for_gun(void) {
    LONG frame = camera_frame;
    volatile LONG *tried = &follow_tried;
    if (*tried == frame || follow_frames[(frame - 1) & 1] != frame - 1) return;
    if (GetCurrentThreadId() == gun_thread) {
        InterlockedIncrement(&body_on_gun_thread);
        return;
    }
    double from = clock_seconds(), waited = 0;
    while (*tried != frame && camera_frame == frame && waited < SIGHT_BODY_WAIT_S) {
        YieldProcessor();
        waited = clock_seconds() - from;
    }
    InterlockedIncrement(*tried == frame ? &body_waited : &body_gave_up);
    if (waited > body_wait_peak) body_wait_peak = waited;
}

static int carry_of(LONG pick, LONG frame, float *out, double *pivot) {
    LONG paired = -1;
    for (LONG f = frame; f >= frame - 1 && paired < 0; f--)
        if (gun_placed[f & 1].frame == f && body_placed[f & 1].frame == f) paired = f;
    if (pick < 0 || paired < 0) return 0;
    const placed_model *body = &body_placed[paired & 1];
    carried_dual(&gun_placed[paired & 1], body, carry_duals[pick & 1], out);
    for (int i = 0; i < 3; i++)
        pivot[i] = -(body->axes[i] * body->place[0] + body->axes[3 + i] * body->place[1] + body->axes[6 + i] * body->place[2]) /
                   (body->scale * body->scale);
    return 1;
}

static void level_dual(const float *dual, const double *pivot, double share, float *out) {
    double q[4] = {dual[0], dual[1], dual[2], dual[3]}, spun[3], shift[3], spin[3], part[4], at[3], t[3];
    float moved[3];
    translation_of(dual, moved);
    if (q[3] < 0)
        for (int i = 0; i < 4; i++) q[i] = -q[i];
    rotate(q, pivot, spun);
    double sine = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]), angle = 2 * atan2(sine, q[3]);
    for (int i = 0; i < 3; i++) {
        shift[i] = spun[i] + moved[i] - pivot[i];
        spin[i] = (sine > 1e-12 ? q[i] / sine * angle : 2 * q[i]) * share;
    }
    quat_of(spin, part);
    rotate(part, pivot, at);
    for (int i = 0; i < 3; i++) t[i] = pivot[i] - at[i] + share * shift[i];
    dual_of(part, t, out);
}

static int joint_under(const short *parents, int joint, int above) {
    while (joint >= 0 && joint != above) joint = parents[joint];
    return joint == above;
}

static int pin_table_build(pin_table *table, void *skeleton, UINT32 joints) {
    static short parents[MAX_SHIFTED_JOINTS];
    void **slots = *(void ***)skeleton;
    if (slots[SKELETON_PARENT_SLOT] != pin_parent_fn || slots[SKELETON_NAME_SLOT] != pin_name_fn) note("pin: this skeleton has its own parent or name function");
    int (*parent_of)(void *, int) = (int (*)(void *, int))slots[SKELETON_PARENT_SLOT];
    const char *(*name_of)(void *, int) = (const char *(*)(void *, int))slots[SKELETON_NAME_SLOT];
    int hips = -1, spine = -1, hand = -1;
    for (UINT32 j = 0; j < joints; j++) {
        short parent = (short)parent_of(skeleton, (int)j);
        const char *name = name_of(skeleton, (int)j);
        if (parent >= (int)j || !name) return 0;
        parents[j] = parent;
        table->level[j] = parent >= 0 ? table->level[parent] : 0;
        for (size_t n = 0; n < sizeof(JOINT_LEVELS) / sizeof(JOINT_LEVELS[0]); n++)
            if (!strcmp(name, JOINT_LEVELS[n].name)) table->level[j] = JOINT_LEVELS[n].level;
        if (!strcmp(name, "Hips")) hips = (int)j;
        if (!strcmp(name, "Spine4")) spine = (int)j;
        if (!strcmp(name, "RightHand")) hand = (int)j;
    }
    return hips >= 0 && spine >= 0 && hand >= 0 && joint_under(parents, hand, spine) && joint_under(parents, spine, hips);
}

static const unsigned char *pin_levels(void *skeleton, UINT32 joints) {
    while (InterlockedCompareExchange(&sight_lock, 1, 0)) YieldProcessor();
    pin_table *table = NULL;
    for (int i = 0; i < PIN_TABLES && !table; i++)
        if (pin_tables[i].skeleton == skeleton && pin_tables[i].joints == joints) table = &pin_tables[i];
    int fresh = !table;
    if (fresh) {
        table = &pin_tables[pin_tables_next++ % PIN_TABLES];
        table->skeleton = skeleton;
        table->joints = joints;
        table->ok = pin_table_build(table, skeleton, joints);
    }
    int ok = table->ok;
    InterlockedExchange(&sight_lock, 0);
    if (fresh) {
        char line[128];
        snprintf(line, sizeof(line), "pin: body skeleton %p has %u joints, %s", skeleton, joints, ok ? "levels ready" : "names or parents refused, the pin stays off");
        note(line);
    }
    if (!ok) {
        InterlockedExchange(&pin_ready, 0);
        InterlockedIncrement(&pin_refused);
    }
    return ok ? table->level : NULL;
}

static int follow_of(const void *instance, LONG kind, float *out, LONG *owed, int *leveled, double *pivot) {
    int gun = kind == SHIFT_GUN && instance == sight_instance;
    if (!gun && kind != SHIFT_BODY) return 0;
    if (!gun) wait_for_gun();
    while (InterlockedCompareExchange(&sight_lock, 1, 0)) YieldProcessor();
    LONG frame = camera_frame;
    if (follow_picked_frame != frame) {
        follow_picked_frame = frame;
        int earlier = follow_tried != frame && follow_frames[(frame - 1) & 1] == frame - 1;
        follow_picked = follow_frames[frame & 1] == frame ? frame : earlier ? frame - 1 : -1;
        InterlockedIncrement(gun ? &first_gun : &first_body);
    }
    LONG pick = gun ? (follow_frames[frame & 1] == frame ? frame : -1) : follow_picked;
    int ok = gun ? pick >= 0 : carry_of(pick, frame, out, pivot);
    *leveled = ok && !gun && carry_leveled[pick & 1];
    if (ok && gun) memcpy(out, follow_duals[pick & 1], sizeof(follow_duals[0]));
    if (ok && !gun) InterlockedIncrement(pick == frame ? &follow_same : &follow_late);
    *owed = !gun && follow_tried != frame ? frame : -1;
    InterlockedExchange(&sight_lock, 0);
    return ok;
}

static void slot_drawn(const float *location, const unsigned char *slot, const unsigned char *pass) {
    float at[3] = {location[3], location[7], location[11]};
    float camera[3] = {camera_at[0], camera_at[1], camera_at[2]};
    int body = bits(at[0]) == body_at[0] && bits(at[1]) == body_at[1] && bits(at[2]) == body_at[2];
    void *character = *(void *const *)(slot + SLOT_CHARACTER);
    if (body && character) m4_body_instance = character;
    float x = at[0] - camera[0], y = at[1] - camera[1], z = at[2] - camera[2];
    if (!body && x * x + y * y + z * z > KEEP_RADIUS_SQUARED) return;
    const float *view = check_pass_camera(pass, camera);
    InterlockedIncrement(body ? &body_draws : &near_draws);
    float from_camera[3] = {0, 0, 0}, from_gun[3] = {0, 0, 0};
    if (!body && !lost_from(attachments, &attachment_head, ATTACHMENT_RING, at, from_gun)) {
        note_miss(at);
        return;
    }
    if (!body) InterlockedIncrement(&placed_draws);
    if (lost_from(cameras, &camera_head, CAMERA_RING, camera, from_camera)) InterlockedIncrement(&camera_hits);
    LONG mode = m4_precision;
    double world[3];
    for (int i = 0; i < 3; i++)
        world[i] = (mode & PRECISION_GUN ? (double)from_gun[i] : 0) - (mode & PRECISION_CAMERA ? (double)from_camera[i] : 0);
    float local[3], sight[8];
    if (!to_local(location, world, local)) return;
    if (body) note_body(location, at, camera, world);
    void *instance = character;
    int sighted = !body && view && (mode & PRECISION_SIGHT) && tag_armed && sight_of(location, view, at, camera, world, instance, sight);
    store_shift(instance, local, body ? SHIFT_BODY : SHIFT_GUN, sighted ? sight : NULL);
    for (int i = 0; i < 3; i++) {
        camera_lost[i] = from_camera[i];
        if (!body) gun_lost[i] = from_gun[i];
    }
}

static void translation_of(const float *q, float *t) {
    t[0] = 2 * (q[3] * q[4] - q[7] * q[0] - (q[5] * q[2] - q[6] * q[1]));
    t[1] = 2 * (q[3] * q[5] - q[7] * q[1] - (q[6] * q[0] - q[4] * q[2]));
    t[2] = 2 * (q[3] * q[6] - q[7] * q[2] - (q[4] * q[1] - q[5] * q[0]));
}

static void shift_dual_quat(float *q, const float *d) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    q[4] += 0.5f * (d[0] * w + d[1] * z - d[2] * y);
    q[5] += 0.5f * (-d[0] * z + d[1] * w + d[2] * x);
    q[6] += 0.5f * (d[0] * y - d[1] * x + d[2] * w);
    q[7] -= 0.5f * (d[0] * x + d[1] * y + d[2] * z);
}

typedef struct {
    LONG frame;
    UINT32 joints;
    float *quat, carry[8], shift[3];
    int leveled;
    double pivot[3];
    const unsigned char *levels;
} owed_body;

static owed_body bodies_owed[BODY_OWED] = {{-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}};

static void refix_dual(const float *to, const float *from, const float *shift, float *fix) {
    float undo[8] = {-from[0], -from[1], -from[2], from[3], -from[4], -from[5], -from[6], from[7]}, back[3];
    for (int i = 0; i < 3; i++) back[i] = -shift[i];
    for (int i = 0; i < 8; i++) fix[i] = i == 3;
    shift_dual_quat(fix, back);
    dual_times(undo, fix);
    dual_times(to, fix);
    shift_dual_quat(fix, shift);
}

static void refix_body(owed_body *owed) {
    float fresh[8] = {0, 0, 0, 1, 0, 0, 0, 0};
    double pivot[3] = {0, 0, 0};
    LONG pick = follow_frames[owed->frame & 1] == owed->frame ? owed->frame : -1;
    int leveled = carry_of(pick, owed->frame, fresh, pivot) && carry_leveled[pick & 1];
    if ((leveled || owed->leveled) && !owed->levels) {
        owed->frame = -1;
        return;
    }
    if (leveled || owed->leveled) {
        float fixes[PIN_LEVELS][8];
        for (int k = 0; k < PIN_LEVELS; k++) {
            float from[8], to[8];
            if (owed->leveled) level_dual(owed->carry, owed->pivot, level_share(k), from);
            else memcpy(from, owed->carry, sizeof(from));
            if (leveled) level_dual(fresh, pivot, level_share(k), to);
            else memcpy(to, fresh, sizeof(to));
            refix_dual(to, from, owed->shift, fixes[k]);
        }
        for (UINT32 j = 0; j < owed->joints; j++) dual_times(fixes[owed->levels[j]], owed->quat + 8 * j);
    } else {
        float fix[8];
        refix_dual(fresh, owed->carry, owed->shift, fix);
        for (UINT32 j = 0; j < owed->joints; j++) dual_times(fix, owed->quat + 8 * j);
    }
    owed->frame = -1;
    InterlockedIncrement(&body_refixed);
}

static void owe_body(LONG frame, float *quat, UINT32 joints, const float *carry, const float *shift, int leveled, const double *pivot,
                     const unsigned char *levels) {
    while (InterlockedCompareExchange(&sight_lock, 1, 0)) YieldProcessor();
    owed_body *owed = NULL;
    for (int i = 0; i < BODY_OWED && !owed; i++)
        if (bodies_owed[i].frame == frame && bodies_owed[i].quat == quat) owed = &bodies_owed[i];
    for (int i = 0; i < BODY_OWED && !owed; i++)
        if (bodies_owed[i].frame != frame) owed = &bodies_owed[i];
    if (owed) {
        owed->frame = frame;
        owed->joints = joints;
        owed->quat = quat;
        memcpy(owed->carry, carry, sizeof(owed->carry));
        memcpy(owed->shift, shift, sizeof(owed->shift));
        memcpy(owed->pivot, pivot, sizeof(owed->pivot));
        owed->leveled = leveled;
        owed->levels = levels;
        if (follow_tried == frame) refix_body(owed);
    }
    InterlockedExchange(&sight_lock, 0);
}

static void refix_bodies(LONG frame) {
    for (int i = 0; i < BODY_OWED; i++)
        if (bodies_owed[i].frame == frame) refix_body(&bodies_owed[i]);
    if (follow_picked_frame == frame) follow_picked = follow_frames[frame & 1] == frame ? frame : -1;
}

static int sight_rescue(const void *instance, float *sight) {
    while (InterlockedCompareExchange(&sight_lock, 1, 0)) YieldProcessor();
    LONG frame = camera_frame;
    int ok = instance == sight_instance && sight_frame > sight_none && frame - sight_frame <= SHIFT_FRAMES;
    if (ok) memcpy(sight, sight_cached, sizeof(sight_cached));
    InterlockedExchange(&sight_lock, 0);
    return ok;
}

static void skinning_computed(unsigned char *context, const unsigned char *skinning) {
    skinning_computation(context);
    InterlockedIncrement(&skins);
    float d[3], sight[8];
    LONG kind, sighted, age;
    void *instance = *(unsigned char **)(context + CONTEXT_INSTANCE_PLUS_8) - 8;
    int found = shift_of(instance, d, &kind, sight, &sighted, &age) && kind >= SHIFT_GUN && kind <= SHIFT_BODY;
    if (instance == sight_instance && (m4_precision & PRECISION_SIGHT) && tag_armed && (!found || !sighted)) {
        InterlockedIncrement(found ? &gun_bare_skins : &gun_lost_skins);
        if (sight_rescue(instance, sight)) {
            InterlockedIncrement(&gun_rescued_skins);
            if (!found) d[0] = d[1] = d[2] = 0, kind = SHIFT_GUN, age = 0, found = 1;
            sighted = 1;
        }
    }
    if (!found) return;
    void *skeleton = *(void **)(context + CONTEXT_SKELETON);
    UINT32 joints = ((UINT32 (*)(void *))(*(void ***)skeleton)[SKELETON_JOINT_COUNT_SLOT])(skeleton);
    float *quat = *(float **)(context + CONTEXT_BONE_QUATS);
    if (!joints || joints > MAX_SHIFTED_JOINTS || !quat) return;
    if (instance == sight_instance)
        for (int i = 0; i < 8; i++) sight_joint[i] = quat[i];
    if (sighted) {
        for (UINT32 j = 0; j < joints; j++) dual_times(sight, quat + 8 * j);
        InterlockedIncrement(&sight_skins);
        if (age) {
            InterlockedIncrement(&sight_stale);
            InterlockedIncrement(&stale_total);
        }
    }
    float carry[8] = {0, 0, 0, 1, 0, 0, 0, 0};
    LONG owed = -1;
    int leveled = 0;
    double pivot[3] = {0, 0, 0};
    int carried = follow_of(instance, kind, carry, &owed, &leveled, pivot);
    const unsigned char *levels = kind == SHIFT_BODY && pin_ready ? pin_levels(skeleton, joints) : NULL;
    if (carried && leveled && levels) {
        float parts[PIN_LEVELS][8];
        for (int k = 1; k < PIN_LEVELS; k++) level_dual(carry, pivot, level_share(k), parts[k]);
        for (UINT32 j = 0; j < joints; j++)
            if (levels[j]) dual_times(parts[levels[j]], quat + 8 * j);
        InterlockedIncrement(&pin_level_skins);
    } else if (carried && leveled) {
        leveled = 0;
        for (int i = 0; i < 8; i++) carry[i] = i == 3;
    } else if (carried) {
        for (UINT32 j = 0; j < joints; j++) dual_times(carry, quat + 8 * j);
        if (kind == SHIFT_BODY) InterlockedIncrement(&pin_rigid_skins);
    }
    float before[3], after[3];
    translation_of(quat, before);
    for (UINT32 j = 0; j < joints; j++) shift_dual_quat(quat + 8 * j, d);
    translation_of(quat, after);
    float error = 0;
    for (int i = 0; i < 3; i++) {
        float off = after[i] - before[i] - d[i];
        if (off * off > error * error) error = off < 0 ? -off : off;
        shift_seen[kind][i] = d[i];
    }
    if (owed >= 0) owe_body(owed, quat, joints, carry, d, leveled, pivot, levels);
    joints_seen[kind] = joints;
    bones_seen[kind] = *(const UINT32 *)(skinning + SKINNING_BONES);
    unit_seen = quat[0] * quat[0] + quat[1] * quat[1] + quat[2] * quat[2] + quat[3] * quat[3];
    moved_error = error;
    InterlockedIncrement(&shifted[kind]);
}

static unsigned char *stub(unsigned char *at, const unsigned char *replayed, size_t replayed_length,
                           const unsigned char *args, size_t args_length, void *target,
                           const unsigned char *exit_state, size_t exit_length, unsigned char *back) {
    at = emit(at, replayed, replayed_length);
    at = emit(at, SAVE_VOLATILES_IN_0XC0, sizeof(SAVE_VOLATILES_IN_0XC0));
    at = emit(at, args, args_length);
    at = emit_call(at, target);
    at = emit(at, RESTORE_VOLATILES, sizeof(RESTORE_VOLATILES));
    at = emit(at, exit_state, exit_length);
    return emit_jump(at, back);
}

static int precision_install(unsigned char *game) {
    for (size_t i = 0; i < sizeof(precision_signatures) / sizeof(precision_signatures[0]); i++) {
        if (memcmp(game + precision_signatures[i].rva, precision_signatures[i].bytes, precision_signatures[i].length)) {
            note("precision: signature mismatch; no precision hooks installed");
            return 0;
        }
    }
    unsigned char *page = near_page(game);
    if (!page) {
        note("precision: no nearby page; no precision hooks installed");
        return 0;
    }
    unsigned char *camera = page;
    unsigned char *camera_bone = stub(camera, NULL, 0, CAMERA_ARGS_RSP_30_40_4C, sizeof(CAMERA_ARGS_RSP_30_40_4C), (void *)camera_add,
                                      CAMERA_LOOP_EXIT_STATE, sizeof(CAMERA_LOOP_EXIT_STATE),
                                      game + CAMERA_ADD_RVA + ADD_LOOP_LENGTH);
    unsigned char *attachment = stub(camera_bone, NULL, 0, CAMERA_ARGS_RSP_30_40_4C, sizeof(CAMERA_ARGS_RSP_30_40_4C), (void *)camera_add,
                                     CAMERA_LOOP_EXIT_STATE, sizeof(CAMERA_LOOP_EXIT_STATE),
                                     game + CAMERA_BONE_ADD_RVA + ADD_LOOP_LENGTH);
    unsigned char *drawn = stub(attachment, NULL, 0, ATTACHMENT_ARGS_RBP_69_41_5D, sizeof(ATTACHMENT_ARGS_RBP_69_41_5D),
                                 (void *)attachment_add, ATTACHMENT_LOOP_EXIT_STATE, sizeof(ATTACHMENT_LOOP_EXIT_STATE),
                                 game + ATTACHMENT_ADD_RVA + ADD_LOOP_LENGTH);
    unsigned char *skinned = stub(drawn, SLOT_DRAWN_BYTES, SLOT_DRAWN_LENGTH,
                                  DRAWN_ARGS_MATRIX_RBP_38_SLOT_RSI_PASS_R13, sizeof(DRAWN_ARGS_MATRIX_RBP_38_SLOT_RSI_PASS_R13),
                                  (void *)slot_drawn, NULL, 0, game + SLOT_DRAWN_RVA + SLOT_DRAWN_LENGTH);
    unsigned char *end = emit_jump(emit(skinned, SKINNING_ARGS_CONTEXT_RCX_DATA_RDI, sizeof(SKINNING_ARGS_CONTEXT_RCX_DATA_RDI)),
                                   (void *)skinning_computed);
    skinning_computation = (void (*)(unsigned char *))(game + SKINNING_COMPUTATION_RVA);
    FlushInstructionCache(GetCurrentProcess(), page, end - page);
    put_jump(game + CAMERA_ADD_RVA, camera, ADD_LOOP_LENGTH);
    put_jump(game + CAMERA_BONE_ADD_RVA, camera_bone, ADD_LOOP_LENGTH);
    put_jump(game + ATTACHMENT_ADD_RVA, attachment, ADD_LOOP_LENGTH);
    put_jump(game + SLOT_DRAWN_RVA, drawn, SLOT_DRAWN_LENGTH);
    put_call(game + SKINNING_CALL_RVA, skinned, SKINNING_CALL_LENGTH);
    note("precision: camera, attachment, slot draw and skinning hooks installed");
    return 1;
}

static void report(void) {
    LONG adds = InterlockedExchange(&camera_adds, 0), kept = InterlockedExchange(&kept_adds, 0);
    LONG draws = InterlockedExchange(&near_draws, 0), placed = InterlockedExchange(&placed_draws, 0);
    LONG body = InterlockedExchange(&body_draws, 0), camera = InterlockedExchange(&camera_hits, 0);
    LONG computed = InterlockedExchange(&skins, 0), gun = InterlockedExchange(&shifted[SHIFT_GUN], 0);
    LONG body_skins = InterlockedExchange(&shifted[SHIFT_BODY], 0);
    LONG same = InterlockedExchange(&pass_same, 0), differ = InterlockedExchange(&pass_differ, 0);
    float gap = pass_gap, miss = miss_gap;
    pass_gap = 0;
    miss_gap = -1;
    if (!adds && !computed) return;
    char line[1024];
    snprintf(line, sizeof(line),
             "precision %ld: draws near %ld placed %ld body %ld camera hit %ld | skins %ld shifted gun %ld body %ld"
             " | joints gun %u/%u body %u/%u | camera adds %ld kept adds %ld | pass camera +0x%lx same %ld differ %ld max %.3f mm"
             " | miss nearest %.3f mm | lost mm camera %.3f %.3f %.3f gun %.3f %.3f %.3f | shift mm gun %.3f %.3f %.3f body %.3f %.3f %.3f"
             " | unit %.5f moved error %.6f mm",
             (long)m4_precision, (long)draws, (long)placed, (long)body, (long)camera, (long)computed, (long)gun, (long)body_skins,
             joints_seen[SHIFT_GUN], bones_seen[SHIFT_GUN], joints_seen[SHIFT_BODY], bones_seen[SHIFT_BODY], (long)adds, (long)kept,
             (unsigned long)pass_camera_offset, (long)same, (long)differ, gap * 1000, miss * 1000,
             camera_lost[0] * 1000, camera_lost[1] * 1000, camera_lost[2] * 1000, gun_lost[0] * 1000, gun_lost[1] * 1000, gun_lost[2] * 1000,
             shift_seen[SHIFT_GUN][0] * 1000, shift_seen[SHIFT_GUN][1] * 1000, shift_seen[SHIFT_GUN][2] * 1000,
             shift_seen[SHIFT_BODY][0] * 1000, shift_seen[SHIFT_BODY][1] * 1000, shift_seen[SHIFT_BODY][2] * 1000, unit_seen,
             moved_error * 1000);
    note(line);
    LONG frames = InterlockedExchange(&sight_frames, 0), learned = InterlockedExchange(&sight_learned, 0);
    LONG sight_skinned = InterlockedExchange(&sight_skins, 0);
    LONG off = InterlockedExchange(&sight_off, 0), shooting = InterlockedExchange(&sight_shooting, 0);
    LONG turning = InterlockedExchange(&sight_turning, 0), swinging = InterlockedExchange(&sight_swinging, 0);
    LONG mounted = InterlockedExchange(&sight_mounted, 0), followed = InterlockedExchange(&sight_followed, 0);
    LONG walking = InterlockedExchange(&sight_walking, 0), flips = InterlockedExchange(&sight_flips, 0);
    LONG stale = InterlockedExchange(&sight_stale, 0), carried = InterlockedExchange(&follow_same, 0);
    LONG late = InterlockedExchange(&follow_late, 0), gun_first = InterlockedExchange(&first_gun, 0);
    LONG body_first = InterlockedExchange(&first_body, 0), waited = InterlockedExchange(&body_waited, 0);
    LONG gave_up = InterlockedExchange(&body_gave_up, 0), gun_thread_bodies = InterlockedExchange(&body_on_gun_thread, 0),
         refixed = InterlockedExchange(&body_refixed, 0);
    LONG gun_lost_n = InterlockedExchange(&gun_lost_skins, 0), gun_bare_n = InterlockedExchange(&gun_bare_skins, 0),
         gun_rescued_n = InterlockedExchange(&gun_rescued_skins, 0), full = InterlockedExchange(&shift_full, 0);
    double wait_peak = body_wait_peak;
    body_wait_peak = 0;
    double fixed_peak = sight_fixed_peak, learned_peak = sight_learned_peak;
    sight_fixed_peak = sight_learned_peak = 0;
    LONG pinned = InterlockedExchange(&pin_frames, 0), level_skins = InterlockedExchange(&pin_level_skins, 0);
    LONG rigid_skins = InterlockedExchange(&pin_rigid_skins, 0), other_vtables = InterlockedExchange(&pin_other_vtables, 0);
    LONG refused = InterlockedExchange(&pin_refused, 0);
    double raw_peak = pin_raw_peak, turn_peak = pin_turn_peak, move_peak = pin_move_peak;
    pin_raw_peak = pin_turn_peak = pin_move_peak = 0;
    if (pinned || refused) {
        snprintf(line, sizeof(line),
                 "pin: frames %ld ready %ld wanted %ld share %.2f | raw off the goal peak %.1f deg | correction peak %.1f deg %.0f mm"
                 " | body skins leveled %ld rigid %ld refused %ld | gun skins lost %ld bare %ld rescued %ld stale %ld | other vtables %ld | hip %ld %ld %ld aim %ld | sway move %ld tilt %ld lean %ld look %ld aim %ld hz %ld",
                 (long)pinned, (long)pin_ready, (long)pin_wanted, pin_share_value, raw_peak, turn_peak, move_peak, (long)level_skins,
                 (long)rigid_skins, (long)refused, (long)gun_lost_n, (long)gun_bare_n, (long)gun_rescued_n, (long)stale, (long)other_vtables,
                 (long)m4_hip_right, (long)m4_hip_ahead, (long)m4_hip_up, (long)m4_aim_depth, (long)m4_sway_move, (long)m4_sway_tilt, (long)m4_sway_lean,
                 (long)m4_sway_look, (long)m4_sway_aim, (long)m4_sway_hz);
        note(line);
    }
    if (!frames && !mounted) return;
    double mean[8], sway[2];
    for (int i = 0; i < 8; i++) {
        mean[i] = learned ? sight_sum[i] / learned : 0;
        sight_sum[i] = 0;
    }
    for (int i = 0; i < 2; i++) {
        sway[i] = learned ? sight_high[i] - sight_low[i] : 0;
        sight_low[i] = 1e9;
        sight_high[i] = -1e9;
    }
    int bins = 0;
    for (int k = 0; k < SIGHT_KINDS; k++)
        for (int i = 0; i < SIGHT_BINS; i++) bins += sight_bins[k][i].weight > 0;
    float joint[8];
    for (int i = 0; i < 8; i++) joint[i] = sight_joint[i];
    float joint_moved[3];
    translation_of(joint, joint_moved);
    double joint_turn = 2 * acos(fmin(1, fabs(joint[3]))) * DEGREES;
    double joint_mm = sqrt((double)joint_moved[0] * joint_moved[0] + (double)joint_moved[1] * joint_moved[1] + (double)joint_moved[2] * joint_moved[2]) * 1000;
    snprintf(line, sizeof(line),
             "sight: frames %ld skins %ld learned %ld bins %d | learned frames raw mrad rear %.2f %.2f front %.2f %.2f"
             " | fixed rear %.2f %.2f front %.2f %.2f, front sway x %.2f y %.2f | not learned: off %ld shot %ld turning %ld swinging %ld"
             " mounted %ld | turn followed %ld, front peak mrad fixed %.2f learned only %.2f, body carried %ld one frame late %ld, waited %ld gave up %ld on the gun thread %ld refixed %ld peak %.3f ms,"
             " first skin gun %ld body %ld | walking %ld camera-only moves %ld stale skins %ld | gun skins lost %ld bare %ld rescued %ld table full %ld | applied %.3f deg %.2f mm | pitch %.1f kind %ld stance %ld scale %.4f | joint0 %.3f deg %.2f mm | fov %.4f",
             (long)frames, (long)sight_skinned, (long)learned, bins, mean[0], mean[1], mean[2], mean[3], mean[4], mean[5], mean[6],
             mean[7], sway[0], sway[1], (long)off, (long)shooting, (long)turning, (long)swinging, (long)mounted, (long)followed,
             fixed_peak, learned_peak, (long)carried, (long)late, (long)waited, (long)gave_up, (long)gun_thread_bodies, (long)refixed, wait_peak * 1000, (long)gun_first, (long)body_first, (long)walking, (long)flips, (long)stale, (long)gun_lost_n, (long)gun_bare_n, (long)gun_rescued_n, (long)full, sight_turn, sight_move,
             sight_pitch, (long)sight_kind, (long)sight_stance, sight_scale, joint_turn, joint_mm, sight_view[0]);
    note(line);
    sight_trace traces[SIGHT_TRACE_FRAMES];
    while (InterlockedCompareExchange(&sight_lock, 1, 0)) YieldProcessor();
    LONG traced = sight_traced;
    memcpy(traces, sight_traces, sizeof(traces[0]) * traced);
    sight_traced = 0;
    InterlockedExchange(&sight_lock, 0);
    for (LONG i = 0; i < traced; i += 6) {
        char trace[1024];
        int used = snprintf(trace, sizeof(trace),
                            "sight trace, ms frame-ms view-turn-yaw/pitch gun-lag-yaw/pitch raw-yaw/pitch deg gap-mm weight follow kind learned shot-s:");
        for (LONG k = i; k < traced && k < i + 6; k++)
            used += snprintf(trace + used, sizeof(trace) - used, " | %.0f %.1f %+.2f/%+.2f %+.2f/%+.2f %+.2f/%+.2f %.1f %.2f %.2f %d %d %.1f",
                             (traces[k].at - traces[0].at) * 1000, traces[k].dt, traces[k].view_yaw, traces[k].view_pitch,
                             traces[k].gun_yaw, traces[k].gun_pitch, traces[k].raw_yaw, traces[k].raw_pitch, traces[k].gap, traces[k].weight, traces[k].follow,
                             traces[k].kind, traces[k].learned, traces[k].shot_s);
        note(trace);
    }
    if (++sight_reports % SIGHT_DUMP_REPORTS) return;
    for (int k = 0; k < SIGHT_KINDS; k++) {
        char dump[4096];
        int used = snprintf(dump, sizeof(dump), "sight bins kind %d, pitch:weight turn mrad right ahead up, move mm right ahead up:", k);
        int any = 0;
        for (int i = 0; i < SIGHT_BINS && used < (int)sizeof(dump) - 96; i++) {
            const sight_bin *bin = &sight_bins[k][i];
            if (bin->weight < 1) continue;
            any = 1;
            used += snprintf(dump + used, sizeof(dump) - used, " %.0f:%.0f %.2f %.2f %.2f %.2f %.2f %.2f", i * SIGHT_BIN_DEG - 90,
                             bin->weight, bin->value[0] * 1000, bin->value[1] * 1000, bin->value[2] * 1000, bin->value[3] * 1000,
                             bin->value[4] * 1000, bin->value[5] * 1000);
        }
        if (any) note(dump);
    }
}

static void flush_rows(void) {
    static sight_row rows[SIGHT_ROWS];
    while (InterlockedCompareExchange(&sight_lock, 1, 0)) YieldProcessor();
    LONG count = sight_rowed;
    memcpy(rows, sight_rows, sizeof(rows[0]) * count);
    sight_rowed = 0;
    InterlockedExchange(&sight_lock, 0);
    if (!count) return;
    if (!row_file) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s\\m4_sight.csv", game_root);
        if (!(row_file = fopen(path, "w"))) return;
        fputs("at,frame,why,instance,held,kind,stance,learned,flags,dt_ms,pitch_deg,turn_deg_s,angle_deg,gap_mm,weight,follow,shot_s,"
              "raw_rear_x,raw_rear_y,raw_front_x,raw_front_y,rear_x,rear_y,front_x,front_y,camera_x,camera_y,camera_z,stale,pull_mrad,view_yaw_deg,body_yaw_deg,body_age\n",
              row_file);
    }
    for (LONG i = 0; i < count; i++) {
        const sight_row *row = &rows[i];
        fprintf(row_file, "%.4f,%ld,%d,%d,%d,%d,%d,%d,%d,%.2f,%.2f,%.1f,%.2f,%.2f,%.3f,%.3f,%.2f", row->at, (long)row->frame, row->why,
                row->instance, row->held, row->kind, row->stance, row->learned, row->flags, row->dt, row->pitch, row->turn_s, row->angle,
                row->gap, row->weight, row->follow, row->shot_s);
        for (int k = 0; k < 8; k++) fprintf(row_file, ",%.3f", row->screen[k]);
        fprintf(row_file, ",%.3f,%.3f,%.3f,%ld,%.3f,%.4f,%.4f,%ld\n", row->camera[0], row->camera[1], row->camera[2], (long)row->stale,
                row->pull, row->view_yaw, row->body_yaw, (long)row->body_age);
    }
    fflush(row_file);
}

static void flush_pin_rows(void) {
    static pin_row rows[PIN_ROWS];
    while (InterlockedCompareExchange(&sight_lock, 1, 0)) YieldProcessor();
    LONG count = pin_rowed;
    memcpy(rows, pin_rows, sizeof(rows[0]) * count);
    pin_rowed = 0;
    InterlockedExchange(&sight_lock, 0);
    if (!count) return;
    if (!pin_file) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s\\m4_pin.csv", game_root);
        if (!(pin_file = fopen(path, "w"))) return;
        fputs("at,frame,dt_ms,share,stance,aim,raw_deg,raw_right_mm,raw_ahead_mm,raw_up_mm,shown_deg,shown_right_mm,shown_ahead_mm,shown_up_mm,"
              "turn_deg,move_mm,speed_m_s,view_deg_s,pitch_deg,sway_right_mm,sway_ahead_mm,sway_up_mm,sway_pitch_deg,sway_roll_deg,sway_yaw_deg,chest_share\n",
              pin_file);
    }
    for (LONG i = 0; i < count; i++) {
        const pin_row *row = &rows[i];
        fprintf(pin_file, "%.4f,%ld,%.2f,%.3f,%d,%d,%.2f,%.1f,%.1f,%.1f,%.2f,%.1f,%.1f,%.1f,%.2f,%.1f,%.2f,%.1f,%.2f,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.3f\n", row->at, (long)row->frame,
                row->dt, row->share, row->stance, row->aim, row->raw_deg, row->raw_rear[0], row->raw_rear[1], row->raw_rear[2], row->shown_deg,
                row->shown_rear[0], row->shown_rear[1], row->shown_rear[2], row->turn_deg, row->move_mm, row->speed, row->view_s, row->pitch,
                row->sway_move[0], row->sway_move[1], row->sway_move[2], row->sway_turn[0], row->sway_turn[1], row->sway_turn[2], row->chest);
    }
    fflush(pin_file);
}

static void pin_check(unsigned char *game) {
    static const unsigned char PATH_BYTES[] = {0x48, 0x8b, 0x81, 0x38, 0x0a, 0x00, 0x00, 0xc3};
    void **character = (void **)(game + CHARACTER_VTABLE_RVA), **skeleton = (void **)(game + SKELETON_VTABLE_RVA);
    pin_character_vtable = character;
    pin_parent_fn = game + SKELETON_PARENT_RVA;
    pin_name_fn = game + SKELETON_NAME_RVA;
    int ok = character[CHARACTER_PATH_SLOT] == game + CHARACTER_PATH_RVA && !memcmp(game + CHARACTER_PATH_RVA, PATH_BYTES, sizeof(PATH_BYTES)) &&
             skeleton[SKELETON_PARENT_SLOT] == pin_parent_fn && skeleton[SKELETON_NAME_SLOT] == pin_name_fn;
    InterlockedExchange(&pin_ready, ok);
    note(ok ? "pin: character and skeleton tables match, the pin can run" : "pin: character or skeleton table mismatch, the pin stays off");
}

static void mark(LONG count) {
    sight_row row = {.at = sight_clock(), .why = ROW_MARK, .frame = count};
    while (InterlockedCompareExchange(&sight_lock, 1, 0)) YieldProcessor();
    add_row(&row);
    InterlockedExchange(&sight_lock, 0);
    char line[64];
    snprintf(line, sizeof(line), "sight mark %ld at %.4f", (long)count, row.at);
    note(line);
}

DWORD WINAPI precision_hook(LPVOID unused) {
    (void)unused;
    HMODULE game;
    while (!(game = GetModuleHandleA("WHGame.dll"))) Sleep(1);
    if (!precision_install((unsigned char *)game)) return 0;
    pin_check((unsigned char *)game);
    LONG marks = 0;
    int held = 0;
    for (LONG tick = 1;; tick++) {
        Sleep(MARK_POLL_MS);
        int down = (GetAsyncKeyState(MARK_KEY) & 0x8000) != 0;
        if (down && !held) mark(++marks);
        held = down;
        if (tick % (ROW_FLUSH_MS / MARK_POLL_MS) == 0) {
            flush_rows();
            flush_pin_rows();
        }
        if (tick % (REPORT_MS / MARK_POLL_MS) == 0) report();
    }
}
