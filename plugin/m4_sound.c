#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

void note(const char *text);
int m4_shot_fired(void);
void m4_in_hand(void);
void m4_frame(void);
void m4_tag_disarm(void);
void m4_class_flags(int m4, int mounted);
void m4_clock_one_shot(const char *name, long volume_percent);
unsigned char *near_page(unsigned char *base);
unsigned char *emit(unsigned char *at, const void *bytes, size_t length);
unsigned char *emit_call(unsigned char *at, void *target);
unsigned char *emit_jump(unsigned char *at, void *target);
void put_jump(unsigned char *from, unsigned char *to, size_t length);

#define EXECUTE_TRIGGER_RVA 0x8019a4
#define EXECUTE_TRIGGER_STOLEN 14
#define SHOT_TRIGGER 0x97a4f151u
#define M4_SHOT_TRIGGER 0x7c901341u
#define IN_HAND_TRIGGER 0x4954de0cu
#define OUT_OF_HAND_TRIGGER 0xe9be0288u
#define SILENT_TRIGGER 0x8b1f32cau
#define FRAME_TRIGGER 0xe847b054u
#define MOUNTED_FRAME_TRIGGER 0xea61bf5fu

static const unsigned RELOAD_TRIGGERS[] = {
    0xc8dc1f25u, 0x3ef95933u, 0xe17cbc26u, 0x7c22bb53u, 0xa27ba575u, 0xff682733u, 0xe723a476u
};

// The mod's reload marks (c_f_m4a1_reload_* and c_f_m4a1_drop_<surface>) and the WAV each plays.
static const struct { unsigned trigger; const char *wav; long volume; } ONE_SHOTS[] = {
    {0x3d29001bu, "out", 100},
    {0x22c0d25du, "in-1", 100},
    {0x0f2d5396u, "in-2", 100},
    {0x9161ce04u, "assist", 100},
    {0xffdd188bu, "catch", 160},
    {0x29094109u, "drop-grass", 100},
    {0x8d818c76u, "drop-rock", 100},
    {0x86552ed9u, "drop-wood", 100},
    {0x41662a33u, "drop-metal", 100},
    {0xf847c3cau, "drop-plants", 100},
    {0x99cf61a5u, "drop-glass", 100},
    {0xa3f5b544u, "drop-cloth", 100},
    {0x7a37ca20u, "drop-water", 100},
    {0x36a5d06eu, "light", 100}
};

static const unsigned char EXECUTE_TRIGGER_BYTES[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x56, 0x57,
                                                      0x41, 0x56, 0x48, 0x83, 0xec, 0x40, 0x48, 0x8b, 0x71, 0x70};

static const unsigned char SAVE_ARGS[] = {0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x48, 0x83, 0xec, 0x28};
static const unsigned char RESTORE_ARGS[] = {0x48, 0x83, 0xc4, 0x28, 0x41, 0x59, 0x41, 0x58, 0x5a, 0x59, 0x89, 0xc2};

static void *volatile m4_proxy;
volatile LONG tag_armed;
static volatile LONG swapped;

static unsigned on_trigger(void *proxy, unsigned trigger) {
    if (trigger == FRAME_TRIGGER || trigger == MOUNTED_FRAME_TRIGGER) {
        if (proxy && proxy == m4_proxy) {
            m4_class_flags(1, trigger == MOUNTED_FRAME_TRIGGER);
            m4_frame();
        }
        return trigger;
    }
    for (size_t i = 0; i < sizeof(ONE_SHOTS) / sizeof(ONE_SHOTS[0]); i++)
        if (trigger == ONE_SHOTS[i].trigger) {
            m4_clock_one_shot(ONE_SHOTS[i].wav, ONE_SHOTS[i].volume);
            return trigger;
        }
    if (trigger == SHOT_TRIGGER && proxy && proxy == m4_proxy) {
        int clocked = m4_shot_fired();
        if (!InterlockedExchange(&swapped, 1)) note(clocked ? "sound: M4 shot plays on the clock" : "sound: M4 shot plays c_f_m4a1_shot");
        return clocked ? SILENT_TRIGGER : M4_SHOT_TRIGGER;
    }
    if (trigger == IN_HAND_TRIGGER) {
        m4_proxy = proxy;
        InterlockedExchange(&swapped, 0);
        note("sound: M4 in hand");
        InterlockedExchange(&tag_armed, 1);
        m4_in_hand();
        m4_class_flags(1, 0);
    } else if (trigger == OUT_OF_HAND_TRIGGER) {
        m4_proxy = NULL;
        InterlockedExchange(&tag_armed, 0);
        m4_class_flags(0, 0);
        m4_tag_disarm();
        note("sound: M4 out of hand");
    }
    if (proxy && proxy == m4_proxy) {
        for (size_t i = 0; i < sizeof(RELOAD_TRIGGERS) / sizeof(RELOAD_TRIGGERS[0]); i++)
            if (trigger == RELOAD_TRIGGERS[i]) return SILENT_TRIGGER;
        static unsigned observed[128];
        static size_t count;
        size_t i;
        for (i = 0; i < count && observed[i] != trigger; i++) {}
        if (i == count && count < sizeof(observed) / sizeof(observed[0])) {
            observed[count++] = trigger;
            char line[80];
            snprintf(line, sizeof(line), "sound: player trigger 0x%08x", trigger);
            note(line);
        }
    }
    return trigger;
}

static void install(unsigned char *game) {
    char line[160];
    unsigned char *execute = game + EXECUTE_TRIGGER_RVA;
    if (memcmp(execute, EXECUTE_TRIGGER_BYTES, sizeof(EXECUTE_TRIGGER_BYTES))) {
        note("sound: WHGame.dll does not match this build's ExecuteTrigger; hook not installed");
        return;
    }
    unsigned char *entry = near_page(game);
    if (!entry) {
        note("sound: no page near WHGame.dll; hook not installed");
        return;
    }
    unsigned char *at = emit(entry, SAVE_ARGS, sizeof(SAVE_ARGS));
    at = emit_call(at, (void *)on_trigger);
    at = emit(at, RESTORE_ARGS, sizeof(RESTORE_ARGS));
    at = emit(at, EXECUTE_TRIGGER_BYTES, EXECUTE_TRIGGER_STOLEN);
    emit_jump(at, execute + EXECUTE_TRIGGER_STOLEN);
    put_jump(execute, entry, EXECUTE_TRIGGER_STOLEN);
    snprintf(line, sizeof(line), "sound: hook installed at WHGame+0x%x, stub %p", EXECUTE_TRIGGER_RVA, entry);
    note(line);
}

DWORD WINAPI sound_hook(LPVOID unused) {
    (void)unused;
    HMODULE game;
    while (!(game = GetModuleHandleA("WHGame.dll"))) Sleep(1);
    install((unsigned char *)game);
    return 0;
}
