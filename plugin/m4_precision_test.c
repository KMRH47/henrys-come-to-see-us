#include "m4_precision.c"
#include <stdlib.h>
#include <math.h>

#define require(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

void note(const char *text) { puts(text); }

volatile LONG tag_armed;
char game_root[MAX_PATH];
static double since_shot = 1e12, fake_now;
static int stance, aimed = 1;
static double view_norm = 1;
double m4_since_shot_ms(void) { return since_shot; }
int m4_stance(void) { return stance; }
int m4_aim_held(void) { return aimed; }
static double fake_clock(void) { return fake_now; }

static const unsigned char RUN_IN_FRAME[] = {
    0x53, 0x55, 0x56, 0x57, 0x41, 0x55, 0x48, 0x89, 0xe3, 0x48, 0x89, 0xcc, 0x48, 0x89, 0xd5, 0x49,
    0x8b, 0x01, 0x49, 0x8b, 0x71, 0x08, 0x49, 0x8b, 0x79, 0x10, 0x49, 0x8b, 0x49, 0x18, 0x4d, 0x8b,
    0x41, 0x20, 0x4d, 0x8b, 0x59, 0x28, 0x4d, 0x8b, 0x69, 0x30, 0x41, 0xff, 0x51, 0x38, 0x48, 0x89,
    0xdc, 0x41, 0x5d, 0x5f, 0x5e, 0x5d, 0x5b, 0xc3
};
typedef struct { ULONG_PTR rax, rsi, rdi, rcx, r8, r11, r13; unsigned char *code; } entry_state;
typedef void (*frame_runner)(unsigned char *rsp_before_call, unsigned char *rbp, void *unused, entry_state *entry);

typedef struct { ULONG_PTR rax, r8; float xmm0; } exit_state;

static unsigned char *game, *rsp_at_site, *rbp_at_site;
static exit_state *seen;
static frame_runner run;

static void put_exit_probe(unsigned char *at) {
    at[0] = 0x48; at[1] = 0x89; at[2] = 0x05;
    *(INT32 *)(at + 3) = (INT32)((unsigned char *)&seen->rax - (at + 7));
    at += 7;
    at[0] = 0x4c; at[1] = 0x89; at[2] = 0x05;
    *(INT32 *)(at + 3) = (INT32)((unsigned char *)&seen->r8 - (at + 7));
    at += 7;
    at[0] = 0xf3; at[1] = 0x0f; at[2] = 0x11; at[3] = 0x05;
    *(INT32 *)(at + 4) = (INT32)((unsigned char *)&seen->xmm0 - (at + 8));
    at[8] = 0xc3;
}

static const float BODY[3] = {2385.1234f, 2113.4567f, 31.25f};
static const float CAMERA_BONE[3] = {0.0731f, -0.0412f, 1.6219f};
static const float GUN_SLOT[3] = {0.2215f, 0.3519f, 1.4421f};

static const struct {
    const char *name;
    size_t rva;
    int on_rbp;
    int a, b, out;
    const float *a_value, *b_value;
} add_cases[] = {
    {"camera", CAMERA_ADD_RVA, 0, 0x30, 0x40, 0x4c, BODY, CAMERA_BONE},
    {"camera bone", CAMERA_BONE_ADD_RVA, 0, 0x30, 0x40, 0x4c, BODY, CAMERA_BONE},
    {"attachment", ATTACHMENT_ADD_RVA, 1, -0x69, -0x41, -0x5d, GUN_SLOT, BODY},
};
#define ADD_CASES (sizeof(add_cases) / sizeof(add_cases[0]))

static void run_add_with(size_t c, const float *a_value, const float *b_value, float *out) {
    unsigned char *base = add_cases[c].on_rbp ? rbp_at_site : rsp_at_site;
    memcpy(base + add_cases[c].a, a_value, 12);
    memcpy(base + add_cases[c].b, b_value, 12);
    memset(base + add_cases[c].out, 0xcd, 12);
    memset(seen, 0, sizeof(*seen));
    entry_state entry = {.code = game + add_cases[c].rva};
    run(rsp_at_site + 8, rbp_at_site, NULL, &entry);
    memcpy(out, base + add_cases[c].out, 12);
}

static void run_add(size_t c, float *out) {
    run_add_with(c, add_cases[c].a_value, add_cases[c].b_value, out);
}

#define TURN_COS 0.8660254f
#define TURN_SIN 0.5f
#define GUN_SCALE 1.25f
#define JOINTS 3
#define COMPUTED_MARK 0x30

static unsigned char slot[0x100], gun_instance[0x100], other_instance[0x100], body_instance[0x100], pass[0x100];
static float pass_camera[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
#define PASS_CAMERA_AT 0x28
static const float GUN_TURN[12] = {
    GUN_SCALE * TURN_COS, -GUN_SCALE * TURN_SIN, 0, 0,
    GUN_SCALE * TURN_SIN, GUN_SCALE * TURN_COS, 0, 0,
    0, 0, GUN_SCALE, 0
};

static void run_slot_drawn_with(const float *matrix, unsigned char *instance) {
    float *location = (float *)(rbp_at_site - 0x38);
    memcpy(location, matrix, 12 * sizeof(float));
    *(unsigned char **)(slot + SLOT_CHARACTER) = instance;
    memset(seen, 0, sizeof(*seen));
    entry_state entry = {.rax = 0x1234, .rsi = (ULONG_PTR)slot, .r8 = 0x5678, .r13 = (ULONG_PTR)pass, .code = game + SLOT_DRAWN_RVA};
    run(rsp_at_site + 8, rbp_at_site, NULL, &entry);
    require(seen->rax == (ULONG_PTR)location);
    for (int i = 0; i < 12; i++) require(location[i] == matrix[i]);
}

static void run_slot_drawn(const float *at, unsigned char *instance) {
    float matrix[12];
    memcpy(matrix, GUN_TURN, sizeof(GUN_TURN));
    for (int i = 0; i < 3; i++) matrix[3 + 4 * i] = at[i];
    run_slot_drawn_with(matrix, instance);
}

static const float JOINT_TURNS[JOINTS][4] = {
    {0, 0, 0, 1},
    {0.1825742f, 0.3651484f, 0.5477226f, 0.7302967f},
    {-0.5f, 0.5f, 0.5f, 0.5f},
};
static const float JOINT_PLACES[JOINTS][3] = {{0, 0, 0}, {0.12f, -0.03f, 0.07f}, {-0.31f, 0.02f, 0.18f}};
static float quats[JOINTS][8];
static unsigned char context[0x40], skinning[0x80], skeleton[0x10];
static void *skeleton_slots[2];

static void build_quats(float (*into)[8]) {
    for (int j = 0; j < JOINTS; j++) {
        const float *q = JOINT_TURNS[j], *t = JOINT_PLACES[j];
        memcpy(into[j], q, 16);
        into[j][4] = 0.5f * (t[0] * q[3] + t[1] * q[2] - t[2] * q[1]);
        into[j][5] = 0.5f * (-t[0] * q[2] + t[1] * q[3] + t[2] * q[0]);
        into[j][6] = 0.5f * (t[0] * q[1] - t[1] * q[0] + t[2] * q[3]);
        into[j][7] = -0.5f * (t[0] * q[0] + t[1] * q[1] + t[2] * q[2]);
    }
}

static void skin_into(unsigned char *instance, float (*into)[8]) {
    build_quats(into);
    memset(context, 0, sizeof(context));
    *(void **)(context + CONTEXT_SKELETON) = skeleton;
    *(float **)(context + CONTEXT_BONE_QUATS) = &into[0][0];
    *(UINT32 *)(skinning + SKINNING_BONES) = JOINTS;
    entry_state entry = {.rax = (ULONG_PTR)(instance + 8), .rdi = (ULONG_PTR)skinning, .r11 = (ULONG_PTR)(context + 0x38),
                         .code = game + SKINNING_SIGNATURE_RVA};
    run(rsp_at_site + 8, rbp_at_site, NULL, &entry);
    require(context[COMPUTED_MARK] == 1);
    require(*(unsigned char **)(context + CONTEXT_INSTANCE_PLUS_8) == instance + 8);
}

static void run_skinning(unsigned char *instance) {
    skin_into(instance, quats);
}

static void local_shift(const double *world, double *local) {
    for (int i = 0; i < 3; i++) local[i] = 0;
    local[0] = (TURN_COS * world[0] + TURN_SIN * world[1]) / GUN_SCALE;
    local[1] = (-TURN_SIN * world[0] + TURN_COS * world[1]) / GUN_SCALE;
    local[2] = world[2] / GUN_SCALE;
}

static double exact_lost(const float *a, const float *b, const float *sum, int i) {
    return (double)a[i] + (double)b[i] - (double)sum[i];
}

static void axis_turn(const double *axis, double degrees, double *out) {
    double n = sqrt(dot3(axis, axis)), x = axis[0] / n, y = axis[1] / n, z = axis[2] / n, a = degrees / DEGREES;
    double c = cos(a), s = sin(a), t = 1 - c;
    double m[9] = {t * x * x + c, t * x * y - s * z, t * x * z + s * y,
                   t * x * y + s * z, t * y * y + c, t * y * z - s * x,
                   t * x * z - s * y, t * y * z + s * x, t * z * z + c};
    memcpy(out, m, sizeof(m));
}

static void times3(const double *a, const double *b, double *out) {
    double m[9];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) m[3 * i + j] = a[3 * i] * b[j] + a[3 * i + 1] * b[3 + j] + a[3 * i + 2] * b[6 + j];
    memcpy(out, m, sizeof(m));
}

#define SIGHT_TEST_FPS 60.0
#define SIGHT_TEST_FRAMES 608
#define SIGHT_TEST_SWAY_DEG 6.0

static void view_of(double pitch, double yaw, double *right, double *ahead, double *up) {
    double p = pitch / DEGREES, w = yaw / DEGREES;
    double base[3][3] = {{1, 0, 0}, {0, cos(p), sin(p)}, {0, -sin(p), cos(p)}};
    double *out[3] = {right, ahead, up};
    for (int k = 0; k < 3; k++) {
        out[k][0] = cos(w) * base[k][0] - sin(w) * base[k][1];
        out[k][1] = sin(w) * base[k][0] + cos(w) * base[k][1];
        out[k][2] = base[k][2];
    }
}

typedef struct {
    double right[3], ahead[3], up[3];
    float location[12], camera[3], slot[3], eye[3], body[12], gun_quats[JOINTS][8], body_quats[JOINTS][8];
} sight_scene;

static void sight_run(sight_scene *scene, double pitch, double yaw, double roll, double off, double right_mm, double up_mm,
                      double scale, unsigned char *instance, const char *order) {
    view_of(pitch, yaw, scene->right, scene->ahead, scene->up);
    double line[3], axis[3], align[9], twist[9], error[9], gun[9], tilt[3], rear_at[3];
    for (int i = 0; i < 3; i++) line[i] = SIGHT_FRONT[i] - SIGHT_REAR[i];
    double length = sqrt(dot3(line, line));
    for (int i = 0; i < 3; i++) line[i] /= length;
    double level_right[3], level_ahead[3], level_up[3], spin[9], vertical[3] = {0, 0, 1};
    view_of(pitch, 0, level_right, level_ahead, level_up);
    cross3(line, level_ahead, axis);
    axis_turn(axis, atan2(sqrt(dot3(axis, axis)), dot3(line, level_ahead)) * DEGREES, align);
    axis_turn(vertical, yaw, spin);
    times3(spin, align, align);
    axis_turn(scene->ahead, roll, twist);
    for (int i = 0; i < 3; i++) {
        tilt[i] = 0.6 * scene->right[i] + 0.8 * scene->up[i];
        rear_at[i] = 0.31 * scene->ahead[i] + right_mm / 1000 * scene->right[i] + up_mm / 1000 * scene->up[i];
    }
    axis_turn(tilt, off, error);
    times3(twist, align, gun);
    times3(error, gun, gun);
    for (int i = 0; i < 9; i++) gun[i] *= scale;
    float c = (float)cos(yaw / DEGREES), s = (float)sin(yaw / DEGREES);
    float body[12] = {c, -s, 0, BODY[0], s, c, 0, BODY[1], 0, 0, 1, BODY[2]};
    memcpy(scene->body, body, sizeof(body));
    scene->eye[0] = (float)((double)c * CAMERA_BONE[0] - (double)s * CAMERA_BONE[1]);
    scene->eye[1] = (float)((double)s * CAMERA_BONE[0] + (double)c * CAMERA_BONE[1]);
    scene->eye[2] = CAMERA_BONE[2];
    run_add_with(0, BODY, scene->eye, scene->camera);
    float *slot_value = scene->slot, gun_at[3];
    for (int i = 0; i < 3; i++) {
        pass_camera[4 * i] = (float)(scene->right[i] * view_norm);
        pass_camera[4 * i + 1] = (float)(scene->ahead[i] * view_norm);
        pass_camera[4 * i + 2] = (float)(scene->up[i] * view_norm);
        pass_camera[4 * i + 3] = scene->camera[i];
        slot_value[i] = (float)(rear_at[i] - (gun[3 * i] * SIGHT_REAR[0] + gun[3 * i + 1] * SIGHT_REAR[1] + gun[3 * i + 2] * SIGHT_REAR[2]) +
                                scene->eye[i]);
    }
    run_add_with(2, slot_value, BODY, gun_at);
    for (int i = 0; i < 3; i++) {
        for (int k = 0; k < 3; k++) scene->location[4 * i + k] = (float)gun[3 * i + k];
        scene->location[4 * i + 3] = gun_at[i];
    }
    fake_now += 1.0 / SIGHT_TEST_FPS;
    for (const char *step = order; *step; step++) {
        int gun_step = *step == 'g' || *step == 'G';
        if (*step == 'g' || *step == 'b') run_slot_drawn_with(gun_step ? scene->location : scene->body, gun_step ? instance : body_instance);
        if (*step == 'G' || *step == 'B') skin_into(gun_step ? instance : body_instance, gun_step ? scene->gun_quats : scene->body_quats);
    }
}

static double glue_gap(const sight_scene *scene, const double *p) {
    double placed[3], hand[3], gun_moved[3], body_moved[3], gap[3];
    dual_apply(scene->gun_quats[0], p, gun_moved);
    for (int i = 0; i < 3; i++)
        placed[i] = (double)scene->slot[i] + scene->location[4 * i] * p[0] + scene->location[4 * i + 1] * p[1] + scene->location[4 * i + 2] * p[2];
    for (int i = 0; i < 3; i++) hand[i] = scene->body[i] * placed[0] + scene->body[4 + i] * placed[1] + scene->body[8 + i] * placed[2];
    dual_apply(scene->body_quats[0], hand, body_moved);
    for (int i = 0; i < 3; i++)
        gap[i] = (double)scene->location[4 * i + 3] - BODY[i] + scene->location[4 * i] * gun_moved[0] + scene->location[4 * i + 1] * gun_moved[1] +
                 scene->location[4 * i + 2] * gun_moved[2] - scene->body[4 * i] * body_moved[0] - scene->body[4 * i + 1] * body_moved[1] -
                 scene->body[4 * i + 2] * body_moved[2];
    return sqrt(dot3(gap, gap));
}

static double ray_gap(const sight_scene *scene, const double *p, double *along) {
    double moved[3], rel[3], gap[3];
    dual_apply(scene->gun_quats[0], p, moved);
    for (int i = 0; i < 3; i++)
        rel[i] = (double)scene->location[4 * i + 3] - (double)scene->camera[i] + scene->location[4 * i] * moved[0] +
                 scene->location[4 * i + 1] * moved[1] + scene->location[4 * i + 2] * moved[2];
    *along = dot3(rel, scene->ahead);
    for (int i = 0; i < 3; i++) gap[i] = rel[i] - scene->ahead[i] * *along;
    return sqrt(dot3(gap, gap));
}

static void reset_sight(void) {
    memset(sight_bins, 0, sizeof(sight_bins));
    memset(sight_eased, 0, sizeof(sight_eased));
    memset(sight_last_sample, 0, sizeof(sight_last_sample));
    memset(shifts, 0, sizeof(shifts));
    sight_active = sight_steady = 0;
    sight_follow = 0;
    memset(sight_held, 0, sizeof(sight_held));
    sight_settled = sight_traced = sight_rowed = 0;
    saddle_kick = saddle_kick_from = 0;
    saddle_last_shot_ms = 1e12;
    saddle_reach = 0;
    saddle_turned_at = -1e12;
    follow_tried = follow_frames[0] = follow_frames[1] = follow_picked_frame = follow_picked = -1;
    gun_placed[0].frame = gun_placed[1].frame = body_placed[0].frame = body_placed[1].frame = -1;
    for (int i = 0; i < BODY_OWED; i++) bodies_owed[i].frame = -1;
    sight_frame = -1;
    sight_instance = NULL;
    for (int i = 0; i < 8; i++) sight_joint[i] = i == 3;
}

static double row_fix(void) {
    double fix = 0;
    for (LONG i = 0; i < sight_rowed; i++)
        for (int k = 0; k < 4; k++) fix = fmax(fix, fabs(sight_rows[i].screen[4 + k] - sight_rows[i].screen[k]));
    return fix;
}

static int learned_bins_in(int kind) {
    int bins = 0;
    for (int i = 0; i < SIGHT_BINS; i++) bins += sight_bins[kind][i].weight > 0;
    return bins;
}

static int learned_bins(void) {
    int bins = 0;
    for (int k = 0; k < SIGHT_KINDS; k++) bins += learned_bins_in(k);
    return bins;
}

static volatile LONG body_started;

static DWORD WINAPI body_skins(LPVOID unused) {
    (void)unused;
    body_started = 1;
    wait_for_gun();
    return 0;
}

int main(void) {
    game = VirtualAlloc(NULL, 0x1000000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    unsigned char *stack = VirtualAlloc(NULL, 0x20000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    require(game && stack);
    seen = (exit_state *)(game + 0xff0000);
    memcpy(game + 0xfe0000, RUN_IN_FRAME, sizeof(RUN_IN_FRAME));
    run = (frame_runner)(game + 0xfe0000);
    rsp_at_site = stack + 0x10000;
    rbp_at_site = rsp_at_site + 0x200;
    for (size_t i = 0; i < sizeof(precision_signatures) / sizeof(precision_signatures[0]); i++)
        memcpy(game + precision_signatures[i].rva, precision_signatures[i].bytes, precision_signatures[i].length);
    put_exit_probe(game + CAMERA_ADD_RVA + ADD_LOOP_LENGTH);
    put_exit_probe(game + CAMERA_BONE_ADD_RVA + ADD_LOOP_LENGTH);
    put_exit_probe(game + ATTACHMENT_ADD_RVA + ADD_LOOP_LENGTH);
    put_exit_probe(game + SLOT_DRAWN_RVA + SLOT_DRAWN_LENGTH);
    game[SKINNING_SIGNATURE_RVA + sizeof(SKINNING_CALL_BYTES)] = 0xc3;
    *(float **)(pass + PASS_CAMERA_AT) = pass_camera;
    static const unsigned char MARK_COMPUTED[] = {0xc6, 0x41, COMPUTED_MARK, 0x01, 0xc3};
    static const unsigned char RETURN_JOINT_COUNT[] = {0xb8, JOINTS, 0x00, 0x00, 0x00, 0xc3};
    memcpy(game + SKINNING_COMPUTATION_RVA, MARK_COMPUTED, sizeof(MARK_COMPUTED));
    memcpy(game + 0xfd0000, RETURN_JOINT_COUNT, sizeof(RETURN_JOINT_COUNT));
    skeleton_slots[SKELETON_JOINT_COUNT_SLOT] = game + 0xfd0000;
    *(void ***)skeleton = skeleton_slots;

    for (size_t i = 0; i < sizeof(precision_signatures) / sizeof(precision_signatures[0]); i++) {
        game[precision_signatures[i].rva + 3] ^= 1;
        require(!precision_install(game));
        game[precision_signatures[i].rva + 3] ^= 1;
        for (size_t j = 0; j < sizeof(precision_signatures) / sizeof(precision_signatures[0]); j++)
            require(!memcmp(game + precision_signatures[j].rva, precision_signatures[j].bytes, precision_signatures[j].length));
    }

    float before[ADD_CASES][3];
    exit_state before_exit[ADD_CASES];
    for (size_t c = 0; c < ADD_CASES; c++) {
        run_add(c, before[c]);
        before_exit[c] = *seen;
        require(seen->rax == 0xc);
    }
    float camera_float[3], gun_float[3];
    memcpy(camera_float, before[0], 12);
    memcpy(gun_float, before[2], 12);
    for (int i = 0; i < 3; i++) pass_camera[3 + 4 * i] = camera_float[i];
    run_slot_drawn(gun_float, gun_instance);
    run_skinning(gun_instance);

    require(precision_install(game));
    for (size_t c = 0; c < ADD_CASES; c++) {
        float after[3];
        run_add(c, after);
        require(!memcmp(after, before[c], 12));
        require(seen->rax == before_exit[c].rax && bits(seen->xmm0) == bits(before_exit[c].xmm0));
        printf("ok   %s add keeps the float sum\n", add_cases[c].name);
    }
    require(kept_adds == 1 && camera_adds == 2);

    double camera_lost[3], gun_lost[3];
    for (int i = 0; i < 3; i++) {
        camera_lost[i] = exact_lost(BODY, CAMERA_BONE, camera_float, i);
        gun_lost[i] = exact_lost(GUN_SLOT, BODY, gun_float, i);
    }
    float far_gun[3] = {gun_float[0] + 3, gun_float[1], gun_float[2]};
    float unplaced_gun[3] = {gun_float[0], gun_float[1] + 0.25f, gun_float[2]};
    const struct {
        const char *name;
        const float *at;
        unsigned char *drawn, *skinned;
        LONG mode, frames_later;
        int camera_fixed, gun_fixed;
    } shift_cases[] = {
        {"off", gun_float, gun_instance, gun_instance, 0, 0, 0, 0},
        {"camera only", gun_float, gun_instance, gun_instance, PRECISION_CAMERA, 0, 1, 0},
        {"gun only", gun_float, gun_instance, gun_instance, PRECISION_GUN, 0, 0, 1},
        {"camera and gun", gun_float, gun_instance, gun_instance, PRECISION_CAMERA | PRECISION_GUN, 0, 1, 1},
        {"another character keeps its skin", gun_float, gun_instance, other_instance, PRECISION_CAMERA | PRECISION_GUN, 0, 0, 0},
        {"far from the camera", far_gun, other_instance, other_instance, PRECISION_CAMERA | PRECISION_GUN, 0, 0, 0},
        {"not placed by an attachment", unplaced_gun, other_instance, other_instance, PRECISION_CAMERA | PRECISION_GUN, 0, 0, 0},
        {"stale shift", gun_float, gun_instance, gun_instance, PRECISION_CAMERA | PRECISION_GUN, SHIFT_FRAMES + 1, 0, 0},
        {"body takes the camera rounding", BODY, body_instance, body_instance, PRECISION_CAMERA | PRECISION_GUN, 0, 1, 0},
        {"body without the camera fix", BODY, body_instance, body_instance, PRECISION_GUN, 0, 0, 0},
    };
    for (size_t c = 0; c < sizeof(shift_cases) / sizeof(shift_cases[0]); c++) {
        memset(shifts, 0, sizeof(shifts));
        m4_precision = shift_cases[c].mode;
        m4_body_instance = NULL;
        run_slot_drawn(shift_cases[c].at, shift_cases[c].drawn);
        require(m4_body_instance == (shift_cases[c].at == BODY ? (void *)shift_cases[c].drawn : NULL));
        camera_frame += shift_cases[c].frames_later;
        run_skinning(shift_cases[c].skinned);
        double world[3], local[3];
        for (int i = 0; i < 3; i++)
            world[i] = (shift_cases[c].gun_fixed ? gun_lost[i] : 0) - (shift_cases[c].camera_fixed ? camera_lost[i] : 0);
        local_shift(world, local);
        for (int j = 0; j < JOINTS; j++) {
            float moved[3];
            translation_of(quats[j], moved);
            require(!memcmp(quats[j], JOINT_TURNS[j], 16));
            for (int i = 0; i < 3; i++) require(fabs(moved[i] - JOINT_PLACES[j][i] - local[i]) < 3e-7);
        }
        printf("ok   skin shift %s\n", shift_cases[c].name);
    }
    m4_precision = PRECISION_CAMERA | PRECISION_GUN;
    memset(shifts, 0, sizeof(shifts));
    run_slot_drawn(gun_float, gun_instance);
    run_skinning(gun_instance);
    for (int i = 0; i < 3; i++) {
        float moved[3];
        translation_of(quats[2], moved);
        double turned = i == 0 ? TURN_COS * moved[0] - TURN_SIN * moved[1]
                      : i == 1 ? TURN_SIN * moved[0] + TURN_COS * moved[1] : moved[2];
        double drawn = (double)(gun_float[i] - camera_float[i]) + GUN_SCALE * turned;
        double rounded = (double)(gun_float[i] - camera_float[i]) + GUN_SCALE * (i == 0 ? TURN_COS * JOINT_PLACES[2][0] - TURN_SIN * JOINT_PLACES[2][1]
                                                                     : i == 1 ? TURN_SIN * JOINT_PLACES[2][0] + TURN_COS * JOINT_PLACES[2][1]
                                                                     : JOINT_PLACES[2][2]);
        double truth = rounded - (double)(gun_float[i] - camera_float[i]) + (double)GUN_SLOT[i] - (double)CAMERA_BONE[i];
        printf("     axis %d float error %.4f mm, fixed error %.6f mm\n", i, fabs(rounded - truth) * 1000, fabs(drawn - truth) * 1000);
        require(fabs(drawn - truth) < 1e-6);
    }
    puts("ok   the shifted gun sits at its exact offset from the exact camera");
    require(pass_camera_offset == PASS_CAMERA_AT && pass_same > 0 && !pass_differ);
    puts("ok   the pass camera is found and matches the rounded camera");

    enum { KEPT, ON_RAY, SWAYS, LAGS, PULLED, LOCKED, KICKED, LET_GO };
    const struct {
        const char *name;
        double pitch, yaw, roll, off, right_mm, up_mm, scale, sway, later_pitch, shot_ms, turn_deg_s, norm;
        LONG mode, armed;
        int stance, expect;
        double fast_deg_s, lag, fast_s, settle_s;
        int lowered;
        const char *order;
        double lag_grow_deg_s;
    } sight_cases[] = {
        {"level aim puts both sights on the view ray", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 1e12, 0, 1, 7, 1, 0, ON_RAY},
        {"aim 40 deg up on a turned and rolled gun", 40, 70, 25, 0.8, -4, 6, 1, 0, 40, 1e12, 0, 1, 7, 1, 0, ON_RAY},
        {"aim 35 deg down on a scaled gun", -35, -120, -10, 0.4, 2, 2, GUN_SCALE, 0, -35, 1e12, 0, 1, 7, 1, 0, ON_RAY},
        {"a still view with rounded axes learns", 17.3, 41.9, 3, 0.5, 3, -2, 1, 0, 17.3, 1e12, 0, 1 - 1e-6, 7, 1, 0, ON_RAY},
        {"a learned pitch carries to an unlearned one", 10, 30, 5, 0.5, 3, -2, 1, 0, 30, 1e12, 0, 1, 7, 1, 0, ON_RAY},
        {"a crouched aim learns its own bins", -8, 200, 4, 0.7, -3, 4, 1, 0, -8, 1e12, 0, 1, 7, 1, STANCE_CROUCHED, ON_RAY},
        {"the sway survives the fix", 5, 15, 0, 0.5, 3, -2, 1, 0.3, 5, 1e12, 0, 1, 7, 1, 0, SWAYS},
        {"without the sight bit the gun keeps its pose", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 1e12, 0, 1, 3, 1, 0, KEPT},
        {"a gun out of hand keeps its pose", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 1e12, 0, 1, 7, 0, 0, KEPT},
        {"a lowered gun keeps its pose", 0, 0, 0, 40, 5, -3, 1, 0, 0, 1e12, 0, 1, 7, 1, 0, KEPT},
        {"a shot holds the learning", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 0, 0, 1, 7, 1, 0, KEPT},
        {"a turning view holds the learning", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 1e12, 20, 1, 7, 1, 0, KEPT},
        {"without the follow bit a gun on the saddle keeps its pose", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 1e12, 0, 1, 7, 1, STANCE_MOUNTED, KEPT},
        {"a fast turn keeps the sights on the ray", 3, 50, 0, 0.6, 5, -3, 1, 0, 3, 1e12, 0, 1, 15, 1, 0, ON_RAY, 240, 1.5, 0.5},
        {"a slow pan keeps the learned pose", 3, 50, 0, 0.6, 5, -3, 1, 0, 3, 1e12, 0, 1, 15, 1, 0, LAGS, 20, 0.3, 0.5},
        {"a fast turn with the aim let go keeps the learned pose", 3, 50, 0, 0.6, 5, -3, 1, 0, 3, 1e12, 0, 1, 15, 1, 0, LAGS, 240, 0.3, 0.5, 0, 1},
        {"without the follow bit a fast turn keeps the learned pose", 3, 50, 0, 0.6, 5, -3, 1, 0, 3, 1e12, 0, 1, 7, 1, 0, LAGS, 240, 0.3, 0.5},
        {"the sway comes back after a fast turn", 5, 15, 0, 0.5, 3, -2, 1, 0.3, 5, 1e12, 0, 1, 15, 1, 0, SWAYS, 240, 1.5, 0.5, 2},
        {"a still aim carries the hands with the learned fix", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 1e12, 0, 1, 7, 1, 0, ON_RAY, 0, 0, 0, 0, 0, "gGbB"},
        {"a still aim 40 deg up carries the hands when the body skins first", 40, 70, 25, 0.8, -4, 6, 1, 0, 40, 1e12, 0, 1, 7, 1, 0, ON_RAY, 0, 0, 0, 0, 0, "bBgG"},
        {"a crouched still aim carries the hands with the learned fix", -8, 200, 4, 0.7, -3, 4, 1, 0, -8, 1e12, 0, 1, 7, 1, STANCE_CROUCHED, ON_RAY, 0, 0, 0, 0, 0, "bgBG"},
        {"a fast turn carries the hands with the gun", 3, 50, 0, 0, 0, 0, 1, 0, 3, 1e12, 0, 1, 15, 1, 0, ON_RAY, 240, 1.5, 0.5, 0, 0, "gGbB"},
        {"the hands stay on the gun when the body skins first", 3, 50, 0, 0, 0, 0, 1, 0, 3, 1e12, 0, 1, 15, 1, 0, ON_RAY, 240, 1.5, 0.5, 0, 0, "bBgG"},
        {"the hands stay on the gun when the body is drawn first", 3, 50, 0, 0, 0, 0, 1, 0, 3, 1e12, 0, 1, 15, 1, 0, ON_RAY, 240, 1.5, 0.5, 0, 0, "bgBG"},
        {"a growing lag keeps the sights on the ray and the hands on the gun when the body skins first", 3, 50, 0, 0, 0, 0, 1, 0, 3, 1e12, 0, 1, 15, 1, 0, ON_RAY, 240, 1.5, 0.5, 0, 0, "bBgG", 6},
        {"a raise in a fast turn keeps the authored pose", 3, 50, 0, 4, 0, 0, 1, 0, 3, 1e12, 0, 1, 15, 1, 0, KEPT, 240, 1.5, 0.5, 0, 0, "gGbB"},
        {"a held offset in a burst is pulled back with the hands", 0, 30, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 31, 1, 0, PULLED, 0, 0.4, 3, 0, 0, "gGbB"},
        {"a held offset is pulled back when the body skins first", 0, 30, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 31, 1, 0, PULLED, 0, 0.4, 3, 0, 0, "bBgG"},
        {"the hold lets go once the offset ends", 0, 30, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 31, 1, 0, PULLED, 0, 0.4, 3, 3, 0, "gGbB"},
        {"a crouched burst's hold lets go the frame the offset ends", 0, 30, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 31, 1, STANCE_CROUCHED, PULLED, 0, 0.4, 3, 0.05, 0, "gGbB"},
        {"without the hold bit a held offset stays", 0, 30, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 15, 1, 0, LAGS, 0, 0.4, 3},
        {"a gun on the saddle sits on the ray", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 1e12, 0, 1, 31, 1, STANCE_MOUNTED, LOCKED},
        {"a gun on the saddle stays on the ray through a burst", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 0, 0, 1, 31, 1, STANCE_MOUNTED, LOCKED},
        {"a jolt on the saddle keeps the sights and hands on the ray", 3, 50, 0, 0.6, 5, -3, 1, 0, 3, 0, 0, 1, 31, 1, STANCE_MOUNTED, LOCKED, 0, 8, 0.1, 0, 0, "gGbB"},
        {"a shot on the saddle lifts the muzzle and pushes the gun back", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 25, 0, 1, 31, 1, STANCE_MOUNTED, KICKED, 0, 0, 0, 0, 0, "gGbB"},
        {"a fast turn on the saddle keeps a gun falling 45 deg behind on the ray", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 1e12, 0, 1, 31, 1, STANCE_MOUNTED, LOCKED, 600, 0, 0.15, 0, 0, "gGbB", 300},
        {"the saddle lock holds a gun 40 deg behind just after a turn stops", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 1e12, 600, 1, 31, 1, STANCE_MOUNTED, LOCKED, 0, 40, 0.2, 0, 0, "gGbB"},
        {"a gun 40 deg off a still view on the saddle is let go", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 1e12, 0, 1, 31, 1, STANCE_MOUNTED, LET_GO, 0, 40, 0.2, 0, 0, "gGbB"},
        {"a gun still 40 deg behind 0.6 s after a turn is let go", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 1e12, 600, 1, 31, 1, STANCE_MOUNTED, LET_GO, 0, 40, 0.6, 0, 0, "gGbB"},
        {"a lowered gun on the saddle keeps its pose", 0, 0, 0, 40, 5, -3, 1, 0, 0, 0, 0, 1, 31, 1, STANCE_MOUNTED, KEPT},
        {"a lowered gun keeps its pose with the hold", 0, 0, 0, 40, 5, -3, 1, 0, 0, 0, 0, 1, 31, 1, 0, KEPT},
    };
    sight_clock = fake_clock;
    for (size_t c = 0; c < sizeof(sight_cases) / sizeof(sight_cases[0]); c++) {
        reset_sight();
        m4_precision = sight_cases[c].mode;
        tag_armed = sight_cases[c].armed;
        since_shot = sight_cases[c].shot_ms;
        stance = sight_cases[c].stance;
        view_norm = sight_cases[c].norm;
        aimed = !sight_cases[c].lowered;
        const char *order = sight_cases[c].order ? sight_cases[c].order : "gG";
        sight_scene scene;
        const struct { double seconds, yaw_deg_s, lag, grow_deg_s; } phases[] = {
            {SIGHT_TEST_FRAMES / SIGHT_TEST_FPS, sight_cases[c].turn_deg_s, 0, 0},
            {sight_cases[c].fast_s, sight_cases[c].fast_deg_s, sight_cases[c].lag, sight_cases[c].lag_grow_deg_s},
            {sight_cases[c].settle_s, 0, 0, 0},
        };
        double yaw = sight_cases[c].yaw, last_sway = 0;
        int frame = 0;
        for (size_t p = 0; p < sizeof(phases) / sizeof(phases[0]); p++)
            for (int start = frame, end = frame + (int)lround(phases[p].seconds * SIGHT_TEST_FPS); frame < end;
                 frame++, yaw += phases[p].yaw_deg_s / SIGHT_TEST_FPS) {
                last_sway = sight_cases[c].sway * sin(frame * SIGHT_TEST_SWAY_DEG / DEGREES);
                double lag = phases[p].lag + phases[p].grow_deg_s * (frame - start) / SIGHT_TEST_FPS;
                sight_run(&scene, sight_cases[c].pitch, yaw, sight_cases[c].roll, sight_cases[c].off + lag + last_sway,
                          sight_cases[c].right_mm, sight_cases[c].up_mm, sight_cases[c].scale, gun_instance, order);
            }
        if (sight_cases[c].later_pitch != sight_cases[c].pitch) {
            int bins = learned_bins();
            since_shot = 0;
            for (int frame = 0; frame < 60; frame++)
                sight_run(&scene, sight_cases[c].later_pitch, sight_cases[c].yaw, sight_cases[c].roll, sight_cases[c].off,
                          sight_cases[c].right_mm, sight_cases[c].up_mm, sight_cases[c].scale, gun_instance, order);
            require(learned_bins() == bins);
        }
        double rear_along, front_along, rear_gap = ray_gap(&scene, SIGHT_REAR, &rear_along), front_gap = ray_gap(&scene, SIGHT_FRONT, &front_along);
        if (sight_cases[c].expect == KEPT) {
            double world[3];
            float gun_at[3] = {scene.location[3], scene.location[7], scene.location[11]};
            for (int i = 0; i < 3; i++) world[i] = exact_lost(scene.slot, BODY, gun_at, i) - exact_lost(BODY, scene.eye, scene.camera, i);
            float local[3];
            require(to_local(scene.location, world, local));
            for (int j = 0; j < JOINTS; j++) {
                float moved[3];
                translation_of(scene.gun_quats[j], moved);
                require(!memcmp(scene.gun_quats[j], JOINT_TURNS[j], 16));
                for (int i = 0; i < 3; i++) require(fabs(moved[i] - JOINT_PLACES[j][i] - local[i]) < 3e-7);
            }
            require(learned_bins() == 0);
            require(row_fix() < 1e-3);
            printf("ok   sight %s (rear %.3f mm, front %.3f mm off the ray, %ld rows)\n", sight_cases[c].name, rear_gap * 1000,
                   front_gap * 1000, (long)sight_rowed);
        } else if (sight_cases[c].expect == ON_RAY) {
            int kind = (sight_cases[c].stance & STANCE_CROUCHED) << 1;
            require(rear_gap < 2e-6 && front_gap < 2e-6);
            require(rear_along > 0.3 && front_along > rear_along);
            require(learned_bins_in(kind) > 0 && learned_bins() == learned_bins_in(kind));
            const sight_row *last = &sight_rows[sight_rowed - 1];
            require(sight_rowed > 0 && last->why == ROW_SOLVED && last->held && last->kind == kind);
            require(hypot(last->screen[4], last->screen[5]) < 0.01 && hypot(last->screen[6], last->screen[7]) < 0.01);
            require(sight_cases[c].off == 0 || row_fix() > 1);
            printf("ok   sight %s (rear %.4f mm, front %.4f mm off the ray)\n", sight_cases[c].name, rear_gap * 1000, front_gap * 1000);
        } else if (sight_cases[c].expect == LOCKED) {
            const sight_row *last = &sight_rows[sight_rowed - 1];
            require(rear_gap < 2e-6 && front_gap < 2e-6 && learned_bins() == 0);
            require(sight_rowed > 0 && last->why == ROW_SOLVED && last->held && (last->stance & STANCE_MOUNTED));
            printf("ok   sight %s (rear %.4f mm, front %.4f mm off the ray)\n", sight_cases[c].name, rear_gap * 1000, front_gap * 1000);
        } else if (sight_cases[c].expect == LET_GO) {
            const sight_row *last = &sight_rows[sight_rowed - 1];
            require(sight_rowed > 0 && last->why == ROW_OFF_LINE && last->held && (last->stance & STANCE_MOUNTED));
            for (int j = 0; j < JOINTS; j++) require(!memcmp(scene.gun_quats[j], JOINT_TURNS[j], 16));
            printf("ok   sight %s (%.1f deg off the ray, left as posed)\n", sight_cases[c].name, last->angle);
        } else if (sight_cases[c].expect == KICKED) {
            const sight_row *last = &sight_rows[sight_rowed - 1];
            double lifted = atan2(front_gap, front_along - rear_along) * DEGREES, pushed = 0.31 - rear_along;
            require(rear_gap < 2e-6 && fabs(lifted - m4_saddle_lift / 100.0) < 1e-3 && last->screen[7] > 0);
            require(fabs(pushed - m4_saddle_push / 1000.0) < 1e-6);
            printf("ok   sight %s (muzzle up %.3f deg, gun back %.2f mm)\n", sight_cases[c].name, lifted, pushed * 1000);
        } else if (sight_cases[c].expect == SWAYS) {
            double swayed = atan2(front_gap, front_along - rear_along) * DEGREES, expected = fabs(last_sway);
            require(rear_gap < 2e-6 && fabs(swayed - expected) < 0.05 * sight_cases[c].sway);
            printf("ok   sight %s (front %.3f deg off the ray for a %.3f deg sway)\n", sight_cases[c].name, swayed, expected);
        } else if (sight_cases[c].expect == PULLED) {
            double lagged = atan2(front_gap, front_along - rear_along) * DEGREES;
            require(rear_gap < 1e-5 && lagged < 0.01 * sight_cases[c].lag);
            require(sight_cases[c].settle_s > 0 || sight_rows[sight_rowed - 1].pull > 1);
            printf("ok   sight %s (front %.4f deg off the ray for a %.3f deg lag)\n", sight_cases[c].name, lagged, sight_cases[c].lag);
        } else {
            double lagged = atan2(front_gap, front_along - rear_along) * DEGREES;
            require(rear_gap < 2e-6 && fabs(lagged - sight_cases[c].lag) < 0.05 * sight_cases[c].lag);
            printf("ok   sight %s (front %.3f deg off the ray for a %.3f deg lag)\n", sight_cases[c].name, lagged, sight_cases[c].lag);
        }
        if (!strchr(order, 'b')) continue;
        static const double GRIPS[][3] = {{0.140397154076, -0.074617902452, 0.187947916991}, {0.05, -0.2, -0.1}, {0.13, 0.01, 0.42}};
        double glued = 0;
        for (size_t g = 0; g < sizeof(GRIPS) / sizeof(GRIPS[0]); g++) glued = fmax(glued, glue_gap(&scene, GRIPS[g]));
        require(glued < 2e-6);
        printf("ok   sight %s: the hands sit %.5f mm off the gun\n", sight_cases[c].name, glued * 1000);
    }
    static const struct {
        const char *name;
        int other_thread, gun_comes;
        LONG waited, gave_up, same;
    } wait_cases[] = {
        {"a body skinned before the gun waits for the gun's solve", 1, 1, 1, 0, 0},
        {"a body waits no longer than 2 ms for a gun that never comes", 1, 0, 0, 1, 0},
        {"a body skinned on the gun's own thread never waits", 0, 0, 0, 0, 1},
    };
    for (size_t c = 0; c < sizeof(wait_cases) / sizeof(wait_cases[0]); c++) {
        LONG frame = camera_frame;
        follow_frames[(frame - 1) & 1] = frame - 1;
        follow_tried = frame - 1;
        sight_follow = 1;
        gun_thread = GetCurrentThreadId();
        body_waited = body_gave_up = body_on_gun_thread = body_started = 0;
        body_wait_peak = 0;
        if (wait_cases[c].other_thread) {
            HANDLE body = CreateThread(NULL, 0, body_skins, NULL, 0, NULL);
            while (!body_started) YieldProcessor();
            double from = clock_seconds();
            while (clock_seconds() - from < 0.0005) YieldProcessor();
            if (wait_cases[c].gun_comes) follow_tried = frame;
            WaitForSingleObject(body, INFINITE);
            CloseHandle(body);
        } else {
            wait_for_gun();
        }
        require(body_waited == wait_cases[c].waited && body_gave_up == wait_cases[c].gave_up && body_on_gun_thread == wait_cases[c].same);
        require(wait_cases[c].gave_up ? body_wait_peak >= SIGHT_BODY_WAIT_S : body_wait_peak < SIGHT_BODY_WAIT_S);
        printf("ok   %s (waited %.3f ms)\n", wait_cases[c].name, body_wait_peak * 1000);
    }
    return 0;
}
