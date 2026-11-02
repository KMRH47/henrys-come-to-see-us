#include "m4_precision.c"
#include <stdlib.h>
#include <math.h>

#define require(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

static int pin_report_lines;
void note(const char *text) {
    if (!strncmp(text, "pin: frames", 11)) pin_report_lines++;
    puts(text);
}

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
static void *skeleton_slots[SKELETON_NAME_SLOT + 1], *character_slots[CHARACTER_PATH_SLOT + 1];

typedef struct {
    const char *name;
    int parent;
    float share;
} joint_spec;

static const joint_spec PIN_JOINTS[JOINTS] = {{"Hips", -1, 0}, {"Spine4", 0, 1}, {"RightHand", 1, 1}};
static const joint_spec FULL_BODY[] = {
    {"Human_Male", -1, 0}, {"Hips", 0, 0}, {"LeftUpLeg", 1, 0}, {"LeftFoot", 2, 0}, {"Spine", 1, 0.15f}, {"Spine1", 4, 0.35f}, {"Spine2", 5, 0.6f},
    {"Spine3", 6, 0.85f}, {"Spine4", 7, 1}, {"RightShoulder", 8, 1}, {"RightArm", 9, 1}, {"RightHand", 10, 1}, {"RightHandSlot", 11, 1},
    {"Neck", 8, 0.5f}, {"Neck1", 13, 0.25f}, {"Head", 14, 0}, {"camera", 15, 0}, {"Look", 15, 0},
};
static const joint_spec NO_SPINE4[] = {{"Hips", -1, 0}, {"Spine", 0, 0}, {"RightHand", 1, 0}};
static const joint_spec HAND_OFF_THE_SPINE[] = {{"Hips", -1, 0}, {"Spine4", 0, 0}, {"RightHand", 0, 0}};
static const joint_spec PARENT_AFTER_CHILD[] = {{"Hips", 2, 0}, {"Spine4", 0, 0}, {"RightHand", 1, 0}};
static const joint_spec NO_NAME[] = {{"Hips", -1, 0}, {"Spine4", 0, 0}, {NULL, 1, 0}, {"RightHand", 1, 0}};

static int fake_parent(void *self, int id) {
    return (unsigned short)(*(const joint_spec **)((unsigned char *)self + 8))[id].parent;
}

static const char *fake_name(void *self, int id) {
    return (*(const joint_spec **)((unsigned char *)self + 8))[id].name;
}

static const char *fake_path(void *self) {
    return self == (void *)gun_instance ? "Objects/ManMade/Weapons/Firearms/M4Carbine.cdf" : "objects/weapons/sword.cdf";
}

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
#define SIGHT_TEST_ROLL_DEG 18.0
static double sight_depth = 0.31;

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
        rear_at[i] = sight_depth * scene->ahead[i] + right_mm / 1000 * scene->right[i] + up_mm / 1000 * scene->up[i];
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

static double glue_gap(const sight_scene *scene, const double *p, int joint) {
    double placed[3], hand[3], before[3], gun_moved[3], body_moved[3], gap[3];
    float raw[JOINTS][8];
    build_quats(raw);
    float undo[8] = {-raw[joint][0], -raw[joint][1], -raw[joint][2], raw[joint][3], -raw[joint][4], -raw[joint][5], -raw[joint][6], raw[joint][7]};
    dual_apply(scene->gun_quats[0], p, gun_moved);
    for (int i = 0; i < 3; i++)
        placed[i] = (double)scene->slot[i] + scene->location[4 * i] * p[0] + scene->location[4 * i + 1] * p[1] + scene->location[4 * i + 2] * p[2];
    for (int i = 0; i < 3; i++) hand[i] = scene->body[i] * placed[0] + scene->body[4 + i] * placed[1] + scene->body[8 + i] * placed[2];
    dual_apply(undo, hand, before);
    dual_apply(scene->body_quats[joint], before, body_moved);
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

static double gun_cant(const sight_scene *scene) {
    double grip[3] = {0, 1, 0}, moved[3], world[3];
    double q[4] = {scene->gun_quats[0][0], scene->gun_quats[0][1], scene->gun_quats[0][2], scene->gun_quats[0][3]};
    rotate(q, grip, moved);
    for (int i = 0; i < 3; i++)
        world[i] = scene->location[4 * i] * moved[0] + scene->location[4 * i + 1] * moved[1] + scene->location[4 * i + 2] * moved[2];
    return atan2(dot3(world, scene->right), dot3(world, scene->up)) * DEGREES;
}

typedef struct {
    double move[3], turn[3], peak_move[3], peak_turn[3], peak_size, peak_angle, biggest_step;
} sway_seen;

static double sway_axis_right[3] = {1, 0, 0}, sway_axis_ahead[3] = {0, 1, 0}, sway_axis_up[3] = {0, 0, 1};

static void sway_watch(sway_seen *seen, const double *right, const double *ahead, const double *up) {
    double turn[3], move[3];
    sway_pose(right, ahead, up, turn, move);
    double step = sqrt(pow(move[0] - seen->move[0], 2) + pow(move[1] - seen->move[1], 2) + pow(move[2] - seen->move[2], 2));
    seen->biggest_step = fmax(seen->biggest_step, step);
    for (int i = 0; i < 3; i++) {
        seen->move[i] = move[i];
        seen->turn[i] = turn[i];
        if (fabs(move[i]) > fabs(seen->peak_move[i])) seen->peak_move[i] = move[i];
        if (fabs(turn[i]) > fabs(seen->peak_turn[i])) seen->peak_turn[i] = turn[i];
    }
    seen->peak_size = fmax(seen->peak_size, sqrt(dot3(move, move)));
    seen->peak_angle = fmax(seen->peak_angle, sqrt(dot3(turn, turn)) * DEGREES);
}

static void sway_run(sway_seen *seen, double seconds, double fps, const double *velocity, double jump_at, double jump_m, double yaw_deg_s, int aimed) {
    static const double rest[3] = {0, 0, 0};
    double dt = 1 / fps, yaw = 0;
    float camera[3] = {100, 200, 30};
    double exact[3] = {100, 200, 30};
    memset(seen, 0, sizeof(*seen));
    sway_reset(aimed);
    for (int i = 0; i < 3; i++) {
        pin_last_camera[i] = exact[i];
        pin_last_right[i] = sway_axis_right[i];
        pin_last_ahead[i] = sway_axis_ahead[i];
        pin_last_up[i] = sway_axis_up[i];
    }
    int jumped = 0;
    for (double t = 0; t < seconds; t += dt) {
        const double *v = t < 1 ? velocity : rest;
        for (int i = 0; i < 3; i++) exact[i] += v[i] * dt;
        if (jump_m != 0 && !jumped && t >= jump_at) {
            exact[0] += jump_m;
            jumped = 1;
        }
        for (int i = 0; i < 3; i++) camera[i] = (float)exact[i];
        yaw += (t < 1 ? yaw_deg_s : 0) * dt;
        double right[3] = {cos(yaw / DEGREES), sin(yaw / DEGREES), 0}, ahead[3] = {-sin(yaw / DEGREES), cos(yaw / DEGREES), 0}, up[3] = {0, 0, 1};
        sway_step(camera, right, ahead, up, dt, aimed);
        sway_watch(seen, right, ahead, up);
        for (int i = 0; i < 3; i++) {
            pin_last_camera[i] = camera[i];
            pin_last_right[i] = right[i];
            pin_last_ahead[i] = ahead[i];
            pin_last_up[i] = up[i];
        }
    }
}

static void sway_cases(void) {
    enum { STILL, TRAILS, FLIES_BACK, CAPPED, LOOK_LAGS };
    static const struct {
        const char *name;
        double velocity[3], jump_m, yaw_deg_s;
        int aimed, expect;
    } cases[] = {
        {"a still camera leaves the gun still", {0, 0, 0}, 0, 0, 0, STILL},
        {"a teleport does not kick the gun", {0, 0, 0}, 100, 0, 0, STILL},
        {"a strafe right trails the gun left and yaws the muzzle left", {3, 0, 0}, 0, 0, 0, TRAILS},
        {"a run ahead pulls the gun back and dips the muzzle", {0, 5, 0}, 0, 0, 0, FLIES_BACK},
        {"a rise dips the muzzle", {0, 0, 4}, 0, 0, 0, FLIES_BACK},
        {"a sprint from dead stop stays under the limit", {0, 8, 0}, 0, 0, 0, CAPPED},
        {"a look left lags the muzzle right", {0, 0, 0}, 0, 60, 0, LOOK_LAGS},
        {"a flick stays under the look cap", {0, 0, 0}, 0, 1000, 0, LOOK_LAGS},
    };
    m4_sway_move = 20;
    m4_sway_tilt = 100;
    m4_sway_lean = 2;
    m4_sway_look = 15;
    m4_sway_aim = 25;
    m4_sway_hz = 220;
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        sway_seen seen;
        sway_run(&seen, 3, 120, cases[c].velocity, 0.5, cases[c].jump_m, cases[c].yaw_deg_s, cases[c].aimed);
        require(seen.peak_size <= SWAY_LIMIT_M && seen.peak_angle < 12);
        require(seen.biggest_step < 0.006);
        require(fabs(seen.move[0]) + fabs(seen.move[1]) + fabs(seen.move[2]) < 1e-4);
        if (cases[c].expect == STILL) require(seen.peak_size < 1e-4 && seen.peak_angle < 1e-3);
        if (cases[c].expect == TRAILS) require(seen.peak_move[0] < -0.002 && seen.peak_turn[2] > 0 && seen.peak_turn[1] > 0);
        if (cases[c].expect == FLIES_BACK) {
            int axis = cases[c].velocity[1] != 0 ? 1 : 2;
            require(seen.peak_move[axis] < -0.002 && seen.peak_turn[0] < 0);
        }
        if (cases[c].expect == CAPPED) require(seen.peak_size > 0.005);
        if (cases[c].expect == LOOK_LAGS) {
            require(seen.peak_turn[2] * cases[c].yaw_deg_s < 0);
            require(fabs(seen.peak_turn[2]) * DEGREES <= SWAY_LOOK_CAP_DEG * 1.2);
        }
        printf("ok   sway %s (peak %.1f mm, %.2f deg, biggest step %.2f mm)\n", cases[c].name, seen.peak_size * 1000, seen.peak_angle, seen.biggest_step * 1000);
    }
    static const double run[3] = {0, 5, 0};
    sway_seen slow, fast, hip, aim;
    sway_run(&slow, 3, 40, run, 0, 0, 0, 0);
    sway_run(&fast, 3, 144, run, 0, 0, 0, 0);
    require(fabs(slow.peak_size - fast.peak_size) < 0.2 * fast.peak_size);
    printf("ok   sway is the same at 40 and 144 fps (%.1f and %.1f mm)\n", slow.peak_size * 1000, fast.peak_size * 1000);
    sway_run(&hip, 3, 120, run, 0, 0, 0, 0);
    sway_run(&aim, 3, 120, run, 0, 0, 0, 1);
    require(fabs(aim.peak_size - 0.25 * hip.peak_size) < 1e-4);
    printf("ok   the aim keeps a quarter of the sway (%.1f of %.1f mm)\n", aim.peak_size * 1000, hip.peak_size * 1000);
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
    sight_stance_changed_at = -1e9;
    sight_crouched = 0;
    saddle_kick = saddle_kick_from = 0;
    saddle_last_shot_ms = 1e12;
    saddle_reach = 0;
    saddle_turned_at = -1e12;
    saddle_steady = raise_live = 0;
    follow_tried = follow_frames[0] = follow_frames[1] = follow_picked_frame = follow_picked = -1;
    gun_placed[0].frame = gun_placed[1].frame = body_placed[0].frame = body_placed[1].frame = -1;
    for (int i = 0; i < BODY_OWED; i++) bodies_owed[i].frame = -1;
    sight_frame = -1;
    sight_instance = NULL;
    for (int i = 0; i < 8; i++) sight_joint[i] = i == 3;
    pin_wanted = pin_ready = pin_live = pin_rowed = 0;
    pin_frames = pin_level_skins = 0;
    pin_weight = 0;
    pin_weight_at = fake_now;
    pin_share_frame = pin_step_frame = -1;
    carry_leveled[0] = carry_leveled[1] = 0;
    memset(pin_tables, 0, sizeof(pin_tables));
    sight_depth = 0.31;
}

static void pin_pose(const sight_scene *scene, const double *p, double *out) {
    double moved[3], rel[3];
    dual_apply(scene->gun_quats[0], p, moved);
    for (int i = 0; i < 3; i++)
        rel[i] = (double)scene->location[4 * i + 3] - (double)scene->camera[i] + scene->location[4 * i] * moved[0] +
                 scene->location[4 * i + 1] * moved[1] + scene->location[4 * i + 2] * moved[2];
    out[0] = dot3(rel, scene->right);
    out[1] = dot3(rel, scene->ahead);
    out[2] = dot3(rel, scene->up);
}

static void pin_axes(const sight_scene *scene, double *line, double *upward) {
    double d[3], along[3], rear[3], front[3], lift[3], at[3], up_model[3], size;
    for (int i = 0; i < 3; i++) {
        along[i] = SIGHT_FRONT[i] - SIGHT_REAR[i];
        d[i] = DOT_REAR[i] - SIGHT_REAR[i];
    }
    size = sqrt(dot3(along, along));
    for (int i = 0; i < 3; i++) along[i] /= size;
    double part = dot3(d, along);
    for (int i = 0; i < 3; i++) up_model[i] = d[i] - part * along[i];
    size = sqrt(dot3(up_model, up_model));
    for (int i = 0; i < 3; i++) at[i] = SIGHT_REAR[i] + up_model[i] / size;
    pin_pose(scene, SIGHT_REAR, rear);
    pin_pose(scene, SIGHT_FRONT, front);
    pin_pose(scene, at, lift);
    for (int i = 0; i < 3; i++) {
        line[i] = front[i] - rear[i];
        upward[i] = lift[i] - rear[i];
    }
    size = sqrt(dot3(line, line));
    for (int i = 0; i < 3; i++) line[i] /= size;
    size = sqrt(dot3(upward, upward));
    for (int i = 0; i < 3; i++) upward[i] /= size;
}

static double angle_to(const double *v, double x, double y, double z) {
    double axis[3], to[3] = {x, y, z};
    cross3(v, to, axis);
    return atan2(sqrt(dot3(axis, axis)), dot3(v, to)) * DEGREES;
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

static void hit_flinch_cases(void) {
    static const struct { double lost, after, scale, pitch_deg; } cases[] = {
        {10, 0.1, 1, -6},
        {30, 0.1, 1, -6},
        {5, 0.1, 1, -5.1},
        {1, 0.1, 1, -4.38},
        {10, 0.1, 0.25, -1.5},
        {10, 0.9, 1, 0},
        {10, -0.1, 1, 0},
    };
    sight_clock = fake_clock;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        double value[6] = {0, 0, 0, 0, 0, 0};
        fake_now = 100;
        m4_hit_signal(cases[i].lost);
        hit_flinch(100 + cases[i].after, cases[i].scale, value);
        require(fabs(value[0] * DEGREES - cases[i].pitch_deg) < 0.02);
        require(value[4] < 0 || cases[i].pitch_deg == 0);
        hit_down_from = hit_side_from = 0;
    }
    double before[6] = {0, 0, 0, 0, 0, 0}, after[6] = {0, 0, 0, 0, 0, 0};
    fake_now = 100;
    m4_hit_signal(10);
    fake_now = 100.1;
    hit_flinch(100.1, 1, before);
    m4_hit_signal(1);
    hit_flinch(100.1, 1, after);
    for (int i = 0; i < 6; i++) require(fabs(before[i] - after[i]) < 1e-9);
    hit_at = -1e12;
    hit_down_from = hit_side_from = 0;
}

int main(void) {
    hit_flinch_cases();
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
    skeleton_slots[SKELETON_PARENT_SLOT] = (void *)fake_parent;
    skeleton_slots[SKELETON_NAME_SLOT] = (void *)fake_name;
    pin_parent_fn = (void *)fake_parent;
    pin_name_fn = (void *)fake_name;
    *(void ***)skeleton = skeleton_slots;
    *(const joint_spec **)(skeleton + 8) = PIN_JOINTS;
    character_slots[CHARACTER_PATH_SLOT] = (void *)fake_path;
    pin_character_vtable = character_slots;
    *(void ***)gun_instance = character_slots;
    *(void ***)other_instance = character_slots;

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

    enum { KEPT, ON_RAY, SWAYS, LAGS, PULLED, LOCKED, KICKED, LET_GO, STEADY, SMOOTH };
    const struct {
        const char *name;
        double pitch, yaw, roll, off, right_mm, up_mm, scale, sway, later_pitch, shot_ms, turn_deg_s, norm;
        LONG mode, armed;
        int stance, expect;
        double fast_deg_s, lag, fast_s, settle_s;
        int lowered;
        const char *order;
        double lag_grow_deg_s, roll_sway;
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
        {"a standing burst's hold lets go the frame the offset ends", 0, 30, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 31, 1, 0, PULLED, 0, 0.4, 3, 0.05, 0, "gGbB"},
        {"a crouched burst's hold lets go the frame the offset ends", 0, 30, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 31, 1, STANCE_CROUCHED, PULLED, 0, 0.4, 3, 0.05, 0, "gGbB"},
        {"without the hold bit a held offset stays", 0, 30, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 15, 1, 0, LAGS, 0, 0.4, 3},
        {"a gun on the saddle sits on the ray", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 1e12, 0, 1, 31, 1, STANCE_MOUNTED, LOCKED},
        {"a gun on the saddle stays on the ray through a burst", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 0, 0, 1, 31, 1, STANCE_MOUNTED, LOCKED},
        {"a jolt on the saddle keeps the sights and hands on the ray", 3, 50, 0, 0.6, 5, -3, 1, 0, 3, 0, 0, 1, 31, 1, STANCE_MOUNTED, LOCKED, 0, 8, 0.1, 0, 0, "gGbB"},
        {"a shot on the saddle lifts the muzzle and pushes the gun back", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 25, 0, 1, 31, 1, STANCE_MOUNTED, KICKED, 0, 0, 0, 0, 0, "gGbB"},
        {"a fast turn on the saddle keeps a gun falling 45 deg behind on the ray", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 1e12, 0, 1, 31, 1, STANCE_MOUNTED, LOCKED, 600, 0, 0.15, 0, 0, "gGbB", 300},
        {"the saddle lock holds a gun 40 deg behind just after a turn stops", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 1e12, 600, 1, 31, 1, STANCE_MOUNTED, LOCKED, 0, 40, 0.2, 0, 0, "gGbB"},
        {"a gun 40 deg off a still view on the saddle is let go", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 1e12, 0, 1, 31, 1, STANCE_MOUNTED, LET_GO, 0, 40, 0.8, 0, 0, "gGbB"},
        {"a gun still 40 deg behind 0.6 s after a turn is let go", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 1e12, 600, 1, 31, 1, STANCE_MOUNTED, LET_GO, 0, 40, 1.2, 0, 0, "gGbB"},
        {"a gun rolling 10 deg on the saddle keeps a steady roll on the ray", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 1e12, 0, 1, 31, 1, STANCE_MOUNTED, STEADY, 0, 0, 0, 0, 0, "gGbB", 0, 10},
        {"a gun rolling 10 deg on the saddle keeps a steady roll in a fast turn", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 1e12, 0, 1, 31, 1, STANCE_MOUNTED, STEADY, 600, 0, 0.15, 0, 0, "gGbB", 300, 10},
        {"a one-frame 30 deg twitch on the saddle barely moves the gun", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 1e12, 0, 1, 31, 1, STANCE_MOUNTED, SMOOTH, 0, 30, 1 / SIGHT_TEST_FPS, 1, 0, "gGbB"},
        {"a two-frame 20 deg twitch on the saddle barely moves the gun", 0, 0, 0, 0.6, 5, -3, 1, 0, 0, 1e12, 0, 1, 31, 1, STANCE_MOUNTED, SMOOTH, 0, 20, 2 / SIGHT_TEST_FPS, 1, 0, "gGbB"},
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
        double yaw = sight_cases[c].yaw, last_sway = 0, cant_low = 1e9, cant_high = -1e9, strayed = 0;
        int frame = 0;
        for (size_t p = 0; p < sizeof(phases) / sizeof(phases[0]); p++)
            for (int start = frame, end = frame + (int)lround(phases[p].seconds * SIGHT_TEST_FPS); frame < end;
                 frame++, yaw += phases[p].yaw_deg_s / SIGHT_TEST_FPS) {
                last_sway = sight_cases[c].sway * sin(frame * SIGHT_TEST_SWAY_DEG / DEGREES);
                double lag = phases[p].lag + phases[p].grow_deg_s * (frame - start) / SIGHT_TEST_FPS;
                double roll = sight_cases[c].roll + sight_cases[c].roll_sway * sin(frame * SIGHT_TEST_ROLL_DEG / DEGREES);
                sight_run(&scene, sight_cases[c].pitch, yaw, roll, sight_cases[c].off + lag + last_sway,
                          sight_cases[c].right_mm, sight_cases[c].up_mm, sight_cases[c].scale, gun_instance, order);
                if (sight_cases[c].expect == SMOOTH && frame >= SIGHT_TEST_FRAMES) {
                    double rear_at, front_at, front_off = ray_gap(&scene, SIGHT_FRONT, &front_at);
                    ray_gap(&scene, SIGHT_REAR, &rear_at);
                    strayed = fmax(strayed, atan2(front_off, front_at - rear_at) * DEGREES);
                }
                if (sight_cases[c].expect == STEADY && frame >= SIGHT_TEST_FRAMES / 2) {
                    double cant = gun_cant(&scene);
                    cant_low = fmin(cant_low, cant);
                    cant_high = fmax(cant_high, cant);
                }
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
        } else if (sight_cases[c].expect == STEADY) {
            double swing = cant_high - cant_low;
            require(rear_gap < 2e-6 && front_gap < 2e-6 && fabs(rear_along - 0.31) < 1e-4);
            require(swing < 0.2 * 2 * sight_cases[c].roll_sway);
            printf("ok   sight %s (roll swings %.2f deg for a %.0f deg roll swing)\n", sight_cases[c].name, swing, 2 * sight_cases[c].roll_sway);
        } else if (sight_cases[c].expect == SMOOTH) {
            require(rear_gap < 2e-6 && front_gap < 2e-6 && strayed < 0.25 * sight_cases[c].lag);
            printf("ok   sight %s (front strays %.2f deg for a %.0f deg twitch)\n", sight_cases[c].name, strayed, sight_cases[c].lag);
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
        for (size_t g = 0; g < sizeof(GRIPS) / sizeof(GRIPS[0]); g++) glued = fmax(glued, glue_gap(&scene, GRIPS[g], 0));
        require(glued < 2e-6);
        printf("ok   sight %s: the hands sit %.5f mm off the gun\n", sight_cases[c].name, glued * 1000);
    }
    static const struct {
        const char *name;
        const joint_spec *joints;
        UINT32 count;
        int ok;
    } level_cases[] = {
        {"a full skeleton twists from the hips to the chest and keeps the head", FULL_BODY, sizeof(FULL_BODY) / sizeof(FULL_BODY[0]), 1},
        {"a skeleton with no chest joint is refused", NO_SPINE4, 3, 0},
        {"a hand that does not hang off the chest is refused", HAND_OFF_THE_SPINE, 3, 0},
        {"a parent listed after its child is refused", PARENT_AFTER_CHILD, 3, 0},
        {"a joint with no name is refused", NO_NAME, 4, 0},
    };
    for (size_t c = 0; c < sizeof(level_cases) / sizeof(level_cases[0]); c++) {
        static unsigned char fake[0x10];
        reset_sight();
        pin_ready = 1;
        *(void ***)fake = skeleton_slots;
        *(const joint_spec **)(fake + 8) = level_cases[c].joints;
        const unsigned char *levels = pin_levels(fake, level_cases[c].count);
        require(!levels == !level_cases[c].ok && pin_ready == level_cases[c].ok);
        for (UINT32 j = 0; levels && j < level_cases[c].count; j++) require(LEVEL_SHARES[levels[j]] == level_cases[c].joints[j].share);
        require(pin_levels(fake, level_cases[c].count) == levels);
        printf("ok   levels %s\n", level_cases[c].name);
    }

    static const struct {
        const char *name;
        double turn[3], move[3], pivot[3], share;
    } dual_cases[] = {
        {"share 0 leaves a big turn out", {1.2, -0.4, 0.3}, {0.2, -0.1, 0.05}, {0.1, -0.2, 1.6}, 0},
        {"share 1 is the whole correction", {1.2, -0.4, 0.3}, {0.2, -0.1, 0.05}, {0.1, -0.2, 1.6}, 1},
        {"share 0.6 moves the pivot 0.6 of its way", {1.2, -0.4, 0.3}, {0.2, -0.1, 0.05}, {0.1, -0.2, 1.6}, 0.6},
        {"a turn past half a circle takes the short way", {0, 0, 3.5}, {0, 0, 0}, {0, 0, 1.6}, 0.5},
        {"no turn slides by the share", {0, 0, 0}, {0.2, 0.1, 0}, {0, 0, 1.6}, 0.25},
    };
    for (size_t c = 0; c < sizeof(dual_cases) / sizeof(dual_cases[0]); c++) {
        double q[4], moved[3], whole[3], part_at[3], points[][3] = {{0, 0, 0}, {0.4, 0.1, 1.2}, {-0.3, 0.5, 0.2}};
        float dual[8], part[8];
        quat_of(dual_cases[c].turn, q);
        dual_of(q, dual_cases[c].move, dual);
        level_dual(dual, dual_cases[c].pivot, dual_cases[c].share, part);
        dual_apply(dual, dual_cases[c].pivot, whole);
        dual_apply(part, dual_cases[c].pivot, part_at);
        for (int i = 0; i < 3; i++) require(fabs(part_at[i] - dual_cases[c].pivot[i] - dual_cases[c].share * (whole[i] - dual_cases[c].pivot[i])) < 2e-6);
        for (size_t k = 0; k < 3; k++) {
            dual_apply(part, points[k], part_at);
            dual_apply(dual, points[k], moved);
            for (int i = 0; i < 3; i++) {
                if (dual_cases[c].share == 0) require(fabs(part_at[i] - points[k][i]) < 2e-6);
                if (dual_cases[c].share == 1) require(fabs(part_at[i] - moved[i]) < 2e-6);
            }
        }
        require(part[3] >= 0);
        printf("ok   level dual %s\n", dual_cases[c].name);
    }

    enum { PIN_HOLDS, PIN_ON_RAY, PIN_KICK, PIN_NOT_THE_GUN, PIN_OFF, PIN_LETS_GO, PIN_BACK_TO_SIGHTS, PIN_FIRST_FRAME };
    const struct {
        const char *name;
        double pitch, yaw, roll, off, swing_deg, swing_hz, turn_deg_s, depth, scale, shot_ms;
        int stance, aim, expect;
        const char *order;
    } pin_cases[] = {
        {"a still view holds the hip pose", 0, 0, 0, 3, 0, 0, 0, 0.1, 1, 1e12, 1, 0, PIN_HOLDS, "gGbB"},
        {"a chest 70 deg off holds the hip pose", 10, 40, 20, 70, 0, 0, 0, 0.3, 1, 1e12, 1, 0, PIN_HOLDS, "gGbB"},
        {"a chest swinging 60 deg at 5 Hz holds the hip pose", -20, 200, 10, 40, 60, 5, 0, 0.3, 1, 1e12, 1, 0, PIN_HOLDS, "gGbB"},
        {"a fast view turn holds the hip pose", 25, 10, 5, 30, 30, 3, 400, 0.2, 1, 1e12, 0, 0, PIN_HOLDS, "gGbB"},
        {"the hands hold on when the body skins first", 0, 40, 20, 50, 30, 4, 200, 0.3, 1, 1e12, 1, 0, PIN_HOLDS, "bBgG"},
        {"the hands hold on when the body is drawn first", 0, 40, 20, 50, 30, 4, 200, 0.3, 1, 1e12, 1, 0, PIN_HOLDS, "bgBG"},
        {"a scaled gun holds the hip pose", 0, 40, 20, 50, 30, 4, 200, 0.3, GUN_SCALE, 1e12, 1, 0, PIN_HOLDS, "gGbB"},
        {"an aim puts both sights on the ray", 5, 30, 0, 20, 0, 0, 0, 0.3, 1, 1e12, 1, 1, PIN_ON_RAY, "gGbB"},
        {"an aim with the rear behind the eye puts both sights on the ray", 5, 30, 40, 80, 30, 5, 100, -0.05, 1, 1e12, 1, 1, PIN_ON_RAY, "bBgG"},
        {"a shot lifts the muzzle and pushes the gun back", 0, 30, 0, 20, 0, 0, 0, 0.3, 1, 25, 0, 1, PIN_KICK, "gGbB"},
        {"another character near the camera keeps its pose", 0, 0, 0, 20, 0, 0, 0, 0.3, 1, 1e12, 1, 0, PIN_NOT_THE_GUN, "gG"},
        {"an aimed gun on the saddle is not pinned", 0, 0, 0, 0.6, 0, 0, 0, 0.31, 1, 1e12, STANCE_MOUNTED, 1, PIN_OFF, "gGbB"},
        {"a gun at the hip on the saddle holds the hip pose", 10, 40, 20, 70, 0, 0, 0, 0.3, 1, 1e12, STANCE_MOUNTED, 0, PIN_HOLDS, "gGbB"},
        {"the light going out hands a far gun back to its clip", 0, 30, 10, 60, 0, 0, 0, 0.3, 1, 1e12, 1, 0, PIN_LETS_GO, "gGbB"},
        {"the light going out in an aim hands the gun back to the sights", 0, 30, 0, 0.6, 0, 0, 0, 0.31, 1, 1e12, 0, 1, PIN_BACK_TO_SIGHTS, "gGbB"},
        {"the first pinned frame moves nothing", 0, 30, 10, 60, 0, 0, 0, 0.3, 1, 1e12, 1, 0, PIN_FIRST_FRAME, "gGbB"},
    };
    sight_clock = fake_clock;
    for (size_t c = 0; c < sizeof(pin_cases) / sizeof(pin_cases[0]); c++) {
        reset_sight();
        m4_sway_move = m4_sway_lean = m4_sway_look = 0;
        m4_pin_body = 100000;
        m4_precision = PRECISION_CAMERA | PRECISION_GUN | PRECISION_SIGHT | PRECISION_FOLLOW | PRECISION_HOLD | PRECISION_PIN;
        tag_armed = 1;
        since_shot = pin_cases[c].shot_ms;
        stance = pin_cases[c].stance;
        view_norm = 1;
        aimed = pin_cases[c].aim;
        sight_depth = pin_cases[c].depth;
        pin_ready = pin_wanted = 1;
        unsigned char *drawn = pin_cases[c].expect == PIN_NOT_THE_GUN ? other_instance : gun_instance;
        int expect = pin_cases[c].expect, frames = expect == PIN_FIRST_FRAME ? 1 : 120;
        if (expect == PIN_BACK_TO_SIGHTS) pin_wanted = 0;
        if (expect == PIN_BACK_TO_SIGHTS) frames = 660;
        sight_scene scene;
        double yaw = pin_cases[c].yaw, worst_rear = 0, worst_line = 0, worst_up = 0, worst_glue = 0, worst_hips = 0, worst_step = 0, last[3] = {0, 0, 0};
        const double hip[3] = {m4_hip_right / 1000.0, m4_hip_ahead / 1000.0, m4_hip_up / 1000.0}, aim[3] = {0, m4_aim_depth / 1000.0, 0};
        const double *want = pin_cases[c].aim ? aim : hip;
        float raw[JOINTS][8];
        build_quats(raw);
        for (int frame = 0; frame < frames; frame++, yaw += pin_cases[c].turn_deg_s / SIGHT_TEST_FPS) {
            double off = pin_cases[c].off + pin_cases[c].swing_deg * sin(2 * M_PI * pin_cases[c].swing_hz * frame / SIGHT_TEST_FPS);
            sight_run(&scene, pin_cases[c].pitch, yaw, pin_cases[c].roll, off, 0, 0, pin_cases[c].scale, drawn, pin_cases[c].order);
            if (expect == PIN_HOLDS || expect == PIN_ON_RAY) {
                double rear[3];
                pin_pose(&scene, SIGHT_REAR, rear);
                double step = sqrt(pow(rear[0] - last[0], 2) + pow(rear[1] - last[1], 2) + pow(rear[2] - last[2], 2));
                memcpy(last, rear, sizeof(rear));
                if (frame > 0 && frame < 40) worst_step = fmax(worst_step, step);
            }
            if (frame < 60 || expect == PIN_NOT_THE_GUN || expect == PIN_OFF || !strchr(pin_cases[c].order, 'b')) continue;
            static const double GRIPS[][3] = {{0.140397154076, -0.074617902452, 0.187947916991}, {0.05, -0.2, -0.1}, {0.13, 0.01, 0.42}};
            for (size_t g = 0; g < sizeof(GRIPS) / sizeof(GRIPS[0]); g++) {
                worst_glue = fmax(worst_glue, glue_gap(&scene, GRIPS[g], 2));
                worst_glue = fmax(worst_glue, glue_gap(&scene, GRIPS[g], 1));
            }
            for (int k = 0; k < 4; k++) worst_hips = fmax(worst_hips, fabs(scene.body_quats[0][k] - raw[0][k]));
            double rear[3], line[3], upward[3];
            pin_pose(&scene, SIGHT_REAR, rear);
            pin_axes(&scene, line, upward);
            for (int i = 0; i < 3; i++) worst_rear = fmax(worst_rear, fabs(rear[i] - want[i]));
            worst_line = fmax(worst_line, angle_to(line, 0, 1, 0));
            worst_up = fmax(worst_up, angle_to(upward, 0, 0, 1));
        }
        if (expect == PIN_HOLDS || expect == PIN_ON_RAY) {
            require(worst_rear < 1e-5 && worst_line < 0.01 && worst_up < 0.01);
            require(worst_glue < 2e-5);
            require(worst_hips < 2e-6);
            require(worst_step < 0.2);
            require(pin_frames > 0 && pin_rowed > 0 && pin_level_skins > 0);
            if (expect == PIN_ON_RAY) {
                double rear_along, front_along;
                require(ray_gap(&scene, SIGHT_REAR, &rear_along) < 1e-5 && ray_gap(&scene, SIGHT_FRONT, &front_along) < 1e-5);
                require(fabs(rear_along - m4_aim_depth / 1000.0) < 1e-5 && front_along > rear_along);
            }
            printf("ok   pin %s (rear %.4f mm, line %.4f deg, up %.4f deg, hands %.4f mm, biggest fade step %.1f mm)\n", pin_cases[c].name,
                   worst_rear * 1000, worst_line, worst_up, worst_glue * 1000, worst_step * 1000);
        } else if (expect == PIN_KICK) {
            double rear[3], line[3], upward[3];
            pin_pose(&scene, SIGHT_REAR, rear);
            pin_axes(&scene, line, upward);
            double lifted = atan2(line[2], line[1]) * DEGREES;
            require(fabs(lifted - m4_saddle_lift / 100.0) < 1e-3 && fabs(want[1] - rear[1] - m4_saddle_push / 1000.0) < 1e-5);
            printf("ok   pin %s (muzzle up %.3f deg, gun back %.2f mm)\n", pin_cases[c].name, lifted, (want[1] - rear[1]) * 1000);
        } else if (expect == PIN_NOT_THE_GUN) {
            for (int j = 0; j < JOINTS; j++) require(!memcmp(scene.gun_quats[j], JOINT_TURNS[j], 16));
            require(!pin_frames && sight_instance != (void *)other_instance);
            printf("ok   pin %s\n", pin_cases[c].name);
        } else if (expect == PIN_OFF) {
            require(!pin_frames && pin_weight == 0);
            printf("ok   pin %s\n", pin_cases[c].name);
        } else if (expect == PIN_FIRST_FRAME) {
            for (int j = 0; j < JOINTS; j++)
                for (int k = 0; k < 4; k++) require(fabs(scene.gun_quats[j][k] - JOINT_TURNS[j][k]) < 2e-6);
            printf("ok   pin %s\n", pin_cases[c].name);
        } else {
            if (expect == PIN_BACK_TO_SIGHTS) {
                pin_wanted = 1;
                for (int frame = 0; frame < 60; frame++) sight_run(&scene, pin_cases[c].pitch, yaw, pin_cases[c].roll, pin_cases[c].off, 0, 0, 1, drawn, pin_cases[c].order);
                double along;
                require(ray_gap(&scene, SIGHT_REAR, &along) < 1e-5 && fabs(along - m4_aim_depth / 1000.0) < 1e-5);
            }
            pin_wanted = 0;
            for (int frame = 0; frame < 30; frame++) sight_run(&scene, pin_cases[c].pitch, yaw, pin_cases[c].roll, pin_cases[c].off, 0, 0, 1, drawn, pin_cases[c].order);
            require(pin_weight == 0 && !pin_live);
            if (expect == PIN_LETS_GO) {
                for (int j = 0; j < JOINTS; j++) require(!memcmp(scene.gun_quats[j], JOINT_TURNS[j], 16));
            } else {
                double along;
                require(ray_gap(&scene, SIGHT_REAR, &along) < 2e-6 && ray_gap(&scene, SIGHT_FRONT, &along) < 2e-6);
            }
            printf("ok   pin %s\n", pin_cases[c].name);
        }
    }
    sway_cases();
    {
        m4_sway_move = m4_sway_lean = m4_sway_look = 0;
        static const struct {
            const char *name;
            LONG hip_ahead_mm, hip_up_mm, pin_body_mm;
            int chest_glued;
            LONG wanted;
        } body_cases[] = {
            {"a carry inside the allowance keeps the chest rigid with the gun", 83, -39, 5000, 1, 1},
            {"a long carry holds the chest back and the hand stays on it", 900, -39, 190, 0, 1},
            {"a long lift holds the chest back and the hand stays on it", 83, 900, 190, 0, 1},
            {"the default carries no chest on foot and the hand stays on the gun", 83, -39, 0, 0, 1},
            {"a short carry in the saddle leaves the chest with the horse and the hand on the gun", 83, -39, 190, 0, 2},
        };
        for (size_t c = 0; c < sizeof(body_cases) / sizeof(body_cases[0]); c++) {
            reset_sight();
            m4_pin_body = body_cases[c].pin_body_mm;
            m4_hip_ahead = body_cases[c].hip_ahead_mm;
            m4_hip_up = body_cases[c].hip_up_mm;
            m4_precision = PRECISION_CAMERA | PRECISION_GUN | PRECISION_SIGHT | PRECISION_FOLLOW | PRECISION_HOLD | PRECISION_PIN;
            tag_armed = 1;
            since_shot = 1e12;
            stance = 1;
            view_norm = 1;
            aimed = 0;
            sight_depth = 0.3;
            pin_ready = 1;
            pin_wanted = body_cases[c].wanted;
            sight_scene scene;
            for (int frame = 0; frame < 120; frame++) sight_run(&scene, 0, 0, 10, 40, 0, 0, 1, gun_instance, "gGbB");
            static const double GRIP[3] = {0.140397154076, -0.074617902452, 0.187947916991};
            double hand = glue_gap(&scene, GRIP, 2), chest = glue_gap(&scene, GRIP, 1);
            require(hand < 2e-5);
            require(body_cases[c].chest_glued ? chest < 2e-5 : chest > 0.02);
            printf("ok   pin %s (hand %.4f mm, chest %.1f mm off the gun)\n", body_cases[c].name, hand * 1000, chest * 1000);
        }
        pin_wanted = 1;
        pin_chest = 1;
        m4_pin_body = 100000;
        m4_hip_ahead = 83;
        m4_hip_up = -39;
        m4_sway_move = 20;
        m4_sway_lean = 2;
        m4_sway_look = 15;
        pin_rowed = 0;
    }
    {
        m4_sway_move = m4_sway_lean = m4_sway_look = 0;
        static const struct {
            const char *name;
            int stance_from, stance_to, on_ray;
        } stance_cases[] = {
            {"standing up in an aim keeps the sights on the ray through a 9 deg swing", STANCE_CROUCHED, 0, 1},
            {"crouching in an aim keeps the sights on the ray through a 9 deg swing", 0, STANCE_CROUCHED, 1},
            {"a 9 deg swing with no stance change keeps the gun's own pose", 0, 0, 0},
        };
        for (size_t c = 0; c < sizeof(stance_cases) / sizeof(stance_cases[0]); c++) {
            reset_sight();
            m4_precision = PRECISION_CAMERA | PRECISION_GUN | PRECISION_SIGHT | PRECISION_FOLLOW | PRECISION_HOLD | PRECISION_PIN;
            tag_armed = 1;
            since_shot = 1e12;
            stance = stance_cases[c].stance_from;
            view_norm = 1;
            aimed = 1;
            sight_depth = 0.3;
            pin_wanted = 0;
            sight_scene scene;
            double along, worst = 0;
            for (int frame = 0; frame < 120; frame++) sight_run(&scene, 0, 0, 0, 1, 0, 0, 1, gun_instance, "gGbB");
            stance = stance_cases[c].stance_to;
            for (int frame = 0; frame < 10; frame++) {
                sight_run(&scene, 0, 0, 0, 9, 0, 0, 1, gun_instance, "gGbB");
                worst = fmax(worst, ray_gap(&scene, SIGHT_FRONT, &along));
            }
            require(stance_cases[c].on_ray ? worst < 1e-4 : worst > 1e-3);
            printf("ok   %s (front sight %.3f mm off)\n", stance_cases[c].name, worst * 1000);
        }
        stance = 1;
        aimed = 0;
    }
    {
        m4_sway_move = m4_sway_lean = m4_sway_look = 0;
        static const struct {
            const char *name;
            int stance_now;
            double roll, want_up;
        } roll_cases[] = {
            {"an aimed walk's 5 deg roll is taken off the gun", 0, 5, 0.3},
            {"an aimed walk's -4 deg roll is taken off the gun", STANCE_CROUCHED, -4, 0.3},
            {"the saddle keeps its own roll", STANCE_MOUNTED, 5, 99},
        };
        for (size_t c = 0; c < sizeof(roll_cases) / sizeof(roll_cases[0]); c++) {
            reset_sight();
            pin_ready = pin_wanted = 0;
            m4_precision = PRECISION_CAMERA | PRECISION_GUN | PRECISION_SIGHT | PRECISION_FOLLOW | PRECISION_HOLD | PRECISION_PIN;
            tag_armed = 1;
            since_shot = 1e12;
            stance = roll_cases[c].stance_now;
            view_norm = 1;
            aimed = 1;
            sight_depth = 0.3;
            sight_scene scene;
            double line[3], upward[3], up_off = 0, line_off = 0, glue = 0, biggest = 0, was = 0;
            sight_run(&scene, 0, 0, 0, 0, 0, 0, 1, gun_instance, "gGbB");
            pin_axes(&scene, line, upward);
            double base = -atan2(upward[0], upward[2]) * DEGREES;
            for (int frame = 0; frame < 150; frame++) {
                sight_run(&scene, 0, 0, base + (frame < 30 ? 0 : roll_cases[c].roll), 0.5, 2, -1, 1, gun_instance, "gGbB");
                pin_axes(&scene, line, upward);
                double cant = atan2(upward[0], upward[2]) * DEGREES;
                if (frame > 31) biggest = fmax(biggest, fabs(cant - was));
                was = cant;
                if (frame < 120) continue;
                up_off = fmax(up_off, fabs(cant));
                line_off = fmax(line_off, angle_to(line, 0, 1, 0));
                glue = fmax(glue, glue_gap(&scene, SIGHT_REAR, 2));
            }
            if (roll_cases[c].want_up < 90) require(up_off < roll_cases[c].want_up);
            else require(up_off > 3);
            require(line_off < 0.3);
            require(glue < 5e-4);
            require(biggest < 1.5);
            printf("ok   sight %s (gun %.2f deg off upright, line %.3f deg, hands %.3f mm, biggest step %.2f deg)\n", roll_cases[c].name, up_off, line_off, glue * 1000, biggest);
        }
        m4_sway_move = 20;
        m4_sway_lean = 2;
        m4_sway_look = 15;
    }
    {
        reset_sight();
        m4_precision = PRECISION_CAMERA | PRECISION_GUN | PRECISION_SIGHT | PRECISION_FOLLOW | PRECISION_HOLD | PRECISION_PIN;
        tag_armed = 1;
        since_shot = 1e12;
        stance = 1;
        view_norm = 1;
        aimed = 0;
        sight_depth = 0.3;
        pin_ready = pin_wanted = 1;
        sight_scene scene;
        double yaw = 0, worst_glue = 0, off_view = 0;
        for (int frame = 0; frame < 180; frame++, yaw += 120 / SIGHT_TEST_FPS) {
            sight_run(&scene, 0, yaw, 10, 60, 0, 0, 1, gun_instance, "gGbB");
            if (frame < 60) continue;
            static const double GRIPS[][3] = {{0.140397154076, -0.074617902452, 0.187947916991}, {0.05, -0.2, -0.1}, {0.13, 0.01, 0.42}};
            for (size_t g = 0; g < sizeof(GRIPS) / sizeof(GRIPS[0]); g++) {
                worst_glue = fmax(worst_glue, glue_gap(&scene, GRIPS[g], 2));
                worst_glue = fmax(worst_glue, glue_gap(&scene, GRIPS[g], 1));
            }
            double line[3], upward[3];
            pin_axes(&scene, line, upward);
            off_view = fmax(off_view, angle_to(line, 0, 1, 0));
        }
        require(worst_glue < 2e-5);
        require(off_view > 0.5 && off_view < SWAY_LOOK_CAP_DEG * 1.5);
        printf("ok   a swaying gun keeps the hands on it (hands %.4f mm, muzzle %.2f deg off the view on a 120 deg/s turn)\n", worst_glue * 1000, off_view);
    }
    GetTempPathA(MAX_PATH, game_root);
    pin_rowed = 0;
    pin_rows[pin_rowed++] = (pin_row){.at = 1, .frame = 2, .dt = 16, .share = 1, .stance = 1};
    flush_pin_rows();
    char pin_csv[MAX_PATH + 16], header[512], first[512];
    snprintf(pin_csv, sizeof(pin_csv), "%s\\m4_pin.csv", game_root);
    fclose(pin_file);
    pin_file = NULL;
    FILE *written = fopen(pin_csv, "r");
    require(written && fgets(header, sizeof(header), written) && fgets(first, sizeof(first), written));
    fclose(written);
    remove(pin_csv);
    int header_fields = 1, row_fields = 1;
    for (const char *at = header; *at; at++) header_fields += *at == ',';
    for (const char *at = first; *at; at++) row_fields += *at == ',';
    require(header_fields == row_fields);
    printf("ok   m4_pin.csv rows have %d fields like the header\n", row_fields);
    pin_frames = 3;
    camera_adds = 1;
    report();
    require(pin_report_lines == 1);
    puts("ok   the report prints the pin line");
    reset_sight();

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
