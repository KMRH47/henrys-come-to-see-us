#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

void note(const char *text);
unsigned char *near_page(unsigned char *base);
unsigned char *emit(unsigned char *at, const void *bytes, size_t length);
unsigned char *emit_call(unsigned char *at, void *target);
unsigned char *emit_jump(unsigned char *at, void *target);
void put_jump(unsigned char *from, unsigned char *to, size_t length);

extern volatile LONG m4_look_off;
extern void *volatile m4_body_instance;

#define LOOK_GATE_RVA 0x4bcd34
#define LOOK_GATE_LENGTH 13
#define LOOK_CVAR_DISP 3
#define LOOK_CVAR_END 7
#define LOOK_SKIP_DISP 9
static const unsigned char LOOK_GATE_BYTES[] = {
    0x44, 0x39, 0x1d, 0x79, 0x87, 0xe9, 0x04, 0x0f, 0x84, 0xf3, 0x00, 0x00, 0x00, 0x4d, 0x8b, 0x75, 0x00
};

static const unsigned char SAVE_VOLATILES_IN_0XC0[] = {
    0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, 0x48, 0x81, 0xec, 0x88, 0x00,
    0x00, 0x00, 0x0f, 0x11, 0x44, 0x24, 0x20, 0x0f, 0x11, 0x4c, 0x24, 0x30, 0x0f, 0x11, 0x54, 0x24,
    0x40, 0x0f, 0x11, 0x5c, 0x24, 0x50, 0x0f, 0x11, 0x64, 0x24, 0x60, 0x0f, 0x11, 0x6c, 0x24, 0x70
};
static const unsigned char PARAMS_R13_TO_RCX[] = {0x4c, 0x89, 0xe9};
static const unsigned char TEST_EAX_THEN_RESTORE_KEEPING_FLAGS[] = {
    0x85, 0xc0, 0x0f, 0x10, 0x44, 0x24, 0x20, 0x0f, 0x10, 0x4c, 0x24, 0x30, 0x0f, 0x10, 0x54, 0x24,
    0x40, 0x0f, 0x10, 0x5c, 0x24, 0x50, 0x0f, 0x10, 0x64, 0x24, 0x60, 0x0f, 0x10, 0x6c, 0x24, 0x70,
    0x48, 0x8d, 0xa4, 0x24, 0x88, 0x00, 0x00, 0x00, 0x41, 0x5b, 0x41, 0x5a, 0x41, 0x59, 0x41, 0x58,
    0x5a, 0x59, 0x58
};
static const unsigned char JNZ_OVER_ONE_JUMP[] = {0x0f, 0x85, 0x0e, 0x00, 0x00, 0x00};

#define LOOK_CENSUS_MS 5000
#define LOOK_CENSUS_SIZE 128

static volatile LONG *look_cvar;
static LONG look_announced;
static volatile LONG look_census_lock;
static void *look_census[LOOK_CENSUS_SIZE];
static unsigned look_census_count, look_census_skipped;
static ULONGLONG look_census_start;

static void look_count(void *instance, int skipped) {
    while (InterlockedCompareExchange(&look_census_lock, 1, 0)) YieldProcessor();
    ULONGLONG now = GetTickCount64();
    if (!look_census_start) look_census_start = now;
    if (now - look_census_start >= LOOK_CENSUS_MS) {
        char line[160];
        snprintf(line, sizeof(line), "look: in %u s with the M4 drawn, %u characters ran look IK and the player's was skipped %u times",
                 (unsigned)((now - look_census_start) / 1000), look_census_count, look_census_skipped);
        note(line);
        look_census_count = look_census_skipped = 0;
        look_census_start = now;
    }
    unsigned i = 0;
    while (i < look_census_count && look_census[i] != instance) i++;
    if (i == look_census_count && i < LOOK_CENSUS_SIZE) look_census[look_census_count++] = instance;
    look_census_skipped += skipped;
    InterlockedExchange(&look_census_lock, 0);
}

static int look_skip(void *const *params) {
    if (!*look_cvar) return 1;
    void *instance = *params;
    if (!m4_look_off || !instance) return 0;
    int skipped = instance == m4_body_instance;
    look_count(instance, skipped);
    if (!skipped) return 0;
    if (!InterlockedExchange(&look_announced, 1)) note("look: the player's look IK is off while the M4 is in hand; NPCs keep theirs");
    return 1;
}

static int look_install(unsigned char *game) {
    unsigned char *site = game + LOOK_GATE_RVA;
    if (memcmp(site, LOOK_GATE_BYTES, sizeof(LOOK_GATE_BYTES))) {
        note("look: signature mismatch; no look IK hook installed");
        return 0;
    }
    unsigned char *entry = near_page(game);
    if (!entry) {
        note("look: no nearby page; no look IK hook installed");
        return 0;
    }
    look_cvar = (volatile LONG *)(site + LOOK_CVAR_END + *(INT32 *)(site + LOOK_CVAR_DISP));
    unsigned char *skip = site + LOOK_GATE_LENGTH + *(INT32 *)(site + LOOK_SKIP_DISP);
    unsigned char *at = emit(entry, SAVE_VOLATILES_IN_0XC0, sizeof(SAVE_VOLATILES_IN_0XC0));
    at = emit(at, PARAMS_R13_TO_RCX, sizeof(PARAMS_R13_TO_RCX));
    at = emit_call(at, (void *)look_skip);
    at = emit(at, TEST_EAX_THEN_RESTORE_KEEPING_FLAGS, sizeof(TEST_EAX_THEN_RESTORE_KEEPING_FLAGS));
    at = emit(at, JNZ_OVER_ONE_JUMP, sizeof(JNZ_OVER_ONE_JUMP));
    at = emit_jump(at, site + LOOK_GATE_LENGTH);
    at = emit_jump(at, skip);
    FlushInstructionCache(GetCurrentProcess(), entry, at - entry);
    put_jump(site, entry, LOOK_GATE_LENGTH);
    note("look: player look IK gate installed");
    return 1;
}

DWORD WINAPI look_hook(LPVOID unused) {
    (void)unused;
    HMODULE game;
    while (!(game = GetModuleHandleA("WHGame.dll"))) Sleep(1);
    look_install((unsigned char *)game);
    return 0;
}
