#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "m4_weapon.h"

void note(const char *text);
unsigned char *near_page(unsigned char *base);
unsigned char *emit_jump(unsigned char *at, void *target);
void put_call(unsigned char *from, unsigned char *to, size_t length);
void put_jump(unsigned char *from, unsigned char *to, size_t length);
unsigned char *m4_item_record(void *item);
long m4_period_ms(void);
double m4_since_shot_ms(void);
int m4_mag_empty(void);

#define FIRE_CAN_CALL_RVA 0x2aa2c28
static const unsigned char FIRE_CAN_CALL_BYTES[] = {0xe8, 0x47, 0xe1, 0xfa, 0xfd, 0x84, 0xc0};
#define FIRE_CAN_RVA 0xa50d74
static const unsigned char FIRE_CAN_BYTES[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57};
#define FIRE_START_CALL_RVA 0x2aa2c4c
static const unsigned char FIRE_START_CALL_BYTES[] = {0xe8, 0xd7, 0x8f, 0x02, 0x00};
#define FIRE_START_RVA 0x2acbc28
static const unsigned char FIRE_START_BYTES[] = {0x48, 0x83, 0xec, 0x28, 0x48, 0x8b, 0x49, 0x50};
#define SHOOT_RVA 0x28966e8
static const unsigned char SHOOT_BYTES[] = {0xf3, 0x0f, 0x11, 0x5c, 0x24, 0x20, 0x4c, 0x8b, 0xdc, 0x45, 0x89, 0x43};
#define INSTANT_CHARGING_VTABLE_RVA 0x470a7c8
#define CHARGED_RVA 0x289974c
static const unsigned char CHARGED_BYTES[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x8a, 0x81, 0x98};
#define CHARGED_CALL_RVA 0x2898eb9
static const unsigned char CHARGED_CALL_BYTES[] = {0xe8, 0x8e, 0x08, 0x00, 0x00};
#define CHARGED_JUMP_RVA 0x289ccae
static const unsigned char CHARGED_JUMP_BYTES[] = {0xe9, 0x99, 0xca, 0xff, 0xff};
#define EXPANSION_RVA 0x7f3310

volatile LONG64 charged_tick;

static unsigned char *fire_game;
static unsigned char (*can_fire_original)(unsigned char *, unsigned char);
static void (*fire_start_original)(unsigned char *, unsigned char);
static void (*shoot)(unsigned char *, int, int, float, const float *, unsigned char);
static void (*charged_original)(unsigned char *);
static unsigned char *(*expansion_of)(void *);

static unsigned char *m4_state(unsigned char *expansion) {
    unsigned char *item = *(unsigned char **)(expansion + 0x40);
    if (!item || !m4_is_weapon_record(m4_item_record(item))) return NULL;
    unsigned char *main = *(unsigned char **)(expansion + 0x50);
    return main ? *(unsigned char **)(main + 0x98) : NULL;
}

static int charged_early(unsigned char *state) {
    return state && *(unsigned char **)state == fire_game + INSTANT_CHARGING_VTABLE_RVA && !state[0x98] && state[0x99];
}

static int charge_missed(unsigned char *state) {
    return *(unsigned char **)state == fire_game + INSTANT_CHARGING_VTABLE_RVA && !state[0x98] && !state[0x99] &&
           m4_since_shot_ms() >= m4_period_ms();
}

static void charged(unsigned char *state);

static unsigned char can_fire(unsigned char *expansion, unsigned char flag) {
    unsigned char result = can_fire_original(expansion, flag);
    unsigned char *state = m4_state(expansion);
    if (!state) return result;
    if (m4_mag_empty()) {
        note("fire: mag empty, press refused");
        return 0;
    }
    if (!result && charge_missed(state)) {
        note("fire: the engine skipped the Charged event; the plugin raises it");
        charged(state);
    }
    if (!result && charged_early(state)) result = 2;
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    char line[160];
    snprintf(line, sizeof(line), "fire: press qpc_ms=%.3f state WHGame+0x%llx done %d charged %d can %d",
             (double)now.QuadPart * 1000 / frequency.QuadPart,
             state ? (unsigned long long)(*(unsigned char **)state - fire_game) : 0ULL,
             state ? state[0x98] : -1, state ? state[0x99] : -1, result);
    note(line);
    return result;
}

static void fire_start(unsigned char *expansion, unsigned char flag) {
    unsigned char *state = m4_state(expansion);
    static const float no_offset[3] = {0, 0, 0};
    if (charged_early(state)) shoot(state, flag, 1, 1.0f, no_offset, 1);
    else fire_start_original(expansion, flag);
}

static void charged(unsigned char *state) {
    unsigned char was = state[0x99];
    charged_original(state);
    if (was || !state[0x99]) return;
    unsigned char *owner = *(unsigned char **)(state + 0x60);
    unsigned char *actor = ((unsigned char *(*)(void *))(*(void ***)owner)[2])(owner);
    unsigned char *expansion = actor ? expansion_of(*(void **)(actor + 0x280)) : NULL;
    if (!expansion || !m4_state(expansion)) return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    InterlockedExchange64(&charged_tick, now.QuadPart);
}

static int fire_install(unsigned char *game) {
    if (memcmp(game + FIRE_CAN_CALL_RVA, FIRE_CAN_CALL_BYTES, sizeof(FIRE_CAN_CALL_BYTES)) ||
        memcmp(game + FIRE_CAN_RVA, FIRE_CAN_BYTES, sizeof(FIRE_CAN_BYTES)) ||
        memcmp(game + FIRE_START_CALL_RVA, FIRE_START_CALL_BYTES, sizeof(FIRE_START_CALL_BYTES)) ||
        memcmp(game + FIRE_START_RVA, FIRE_START_BYTES, sizeof(FIRE_START_BYTES)) ||
        memcmp(game + SHOOT_RVA, SHOOT_BYTES, sizeof(SHOOT_BYTES)) ||
        memcmp(game + CHARGED_RVA, CHARGED_BYTES, sizeof(CHARGED_BYTES)) ||
        memcmp(game + CHARGED_CALL_RVA, CHARGED_CALL_BYTES, sizeof(CHARGED_CALL_BYTES)) ||
        memcmp(game + CHARGED_JUMP_RVA, CHARGED_JUMP_BYTES, sizeof(CHARGED_JUMP_BYTES))) {
        note("fire: signature mismatch; no fire hooks installed");
        return 0;
    }
    unsigned char *entry = near_page(game);
    if (!entry) {
        note("fire: no nearby page; no fire hooks installed");
        return 0;
    }
    fire_game = game;
    can_fire_original = (void *)(game + FIRE_CAN_RVA);
    fire_start_original = (void *)(game + FIRE_START_RVA);
    shoot = (void *)(game + SHOOT_RVA);
    charged_original = (void *)(game + CHARGED_RVA);
    expansion_of = (void *)(game + EXPANSION_RVA);
    emit_jump(entry, (void *)can_fire);
    emit_jump(entry + 16, (void *)fire_start);
    emit_jump(entry + 32, (void *)charged);
    FlushInstructionCache(GetCurrentProcess(), entry, 46);
    put_call(game + FIRE_CAN_CALL_RVA, entry, 5);
    put_call(game + FIRE_START_CALL_RVA, entry + 16, 5);
    put_call(game + CHARGED_CALL_RVA, entry + 32, 5);
    put_jump(game + CHARGED_JUMP_RVA, entry + 32, 5);
    note("fire: an M4 press may fire once the per-shot charge is charged, and presses wait for it");
    return 1;
}

DWORD WINAPI fire_hook(LPVOID unused) {
    (void)unused;
    HMODULE game;
    while (!(game = GetModuleHandleA("WHGame.dll"))) Sleep(1);
    fire_install((unsigned char *)game);
    return 0;
}
