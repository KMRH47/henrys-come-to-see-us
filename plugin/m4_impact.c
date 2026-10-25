#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

void note(const char *text);
double m4_since_shot_ms(void);

#define MFX_VTABLE_RVA 0x4056c88
#define MFX_BY_NAME_SLOT 4
#define MFX_BY_CUSTOM_SLOT 6
#define MFX_BY_NAME_RVA 0x60c044
#define MFX_BY_CUSTOM_RVA 0x52aa38
#define M4_HIT_WINDOW_MS 1500
#define DUMPED_HITS 8

static const unsigned char MFX_BY_NAME_BYTES[] = {
    0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x4c, 0x8b, 0x0d, 0x47, 0xce, 0xe8, 0x04, 0x33, 0xdb, 0x41
};
static const unsigned char MFX_BY_CUSTOM_BYTES[] = {
    0x4c, 0x8b, 0xdc, 0x49, 0x89, 0x5b, 0x08, 0x49, 0x89, 0x6b, 0x10, 0x49, 0x89, 0x73, 0x18, 0x49
};
static const char VANILLA_LIBRARY[] = "destructibles";
static const char M4_LIBRARY[] = "m4_impacts";
static const char *const BULLET_ROWS[] = {"hit_passed_bullet", "hit_stopped_bullet"};
static const char *const IMPACT_EFFECTS[] = {
    "bullet_wicker", "bullet_shield_wood", "bullet_wood", "bullet_stone", "bullet_ceramics",
    "bullet_sack", "bullet_fabric_items", "bullet_metal", "bullet_plaster", "bullet_straw",
    "bullet_soil", "bullet_mud", "bullet_mudwater", "bullet_grass", "bullet_water",
};

static unsigned short (*effect_by_custom)(void *, const char *, int);
static unsigned short (*effect_by_name)(void *, const char *, const char *);
static volatile LONG dumped;

static int bullet_row(const char *custom) {
    for (size_t i = 0; custom && i < sizeof(BULLET_ROWS) / sizeof(BULLET_ROWS[0]); i++)
        if (!strcmp(custom, BULLET_ROWS[i])) return 1;
    return 0;
}

static unsigned short impact_effect(void *mfx, const char *custom, int surface) {
    unsigned short id = effect_by_custom(mfx, custom, surface);
    if (!id || !bullet_row(custom) || m4_since_shot_ms() >= M4_HIT_WINDOW_MS) return id;
    for (size_t i = 0; i < sizeof(IMPACT_EFFECTS) / sizeof(IMPACT_EFFECTS[0]); i++) {
        if (effect_by_name(mfx, VANILLA_LIBRARY, IMPACT_EFFECTS[i]) != id) continue;
        unsigned short m4 = effect_by_name(mfx, M4_LIBRARY, IMPACT_EFFECTS[i]);
        if (InterlockedIncrement(&dumped) <= DUMPED_HITS) {
            char line[160];
            snprintf(line, sizeof(line), "impact: M4 %s surface %d %s:%s %u -> %s %u",
                     custom, surface, VANILLA_LIBRARY, IMPACT_EFFECTS[i], id, M4_LIBRARY, m4);
            note(line);
        }
        return m4 ? m4 : id;
    }
    return id;
}

static int impact_install(unsigned char *game) {
    void **vtable = (void **)(game + MFX_VTABLE_RVA);
    if (vtable[MFX_BY_NAME_SLOT] != game + MFX_BY_NAME_RVA || vtable[MFX_BY_CUSTOM_SLOT] != game + MFX_BY_CUSTOM_RVA
        || memcmp(game + MFX_BY_NAME_RVA, MFX_BY_NAME_BYTES, sizeof(MFX_BY_NAME_BYTES))
        || memcmp(game + MFX_BY_CUSTOM_RVA, MFX_BY_CUSTOM_BYTES, sizeof(MFX_BY_CUSTOM_BYTES))) {
        note("impact: WHGame.dll does not match this build's material effects table; hook not installed");
        return 0;
    }
    effect_by_name = vtable[MFX_BY_NAME_SLOT];
    effect_by_custom = vtable[MFX_BY_CUSTOM_SLOT];
    DWORD old;
    if (!VirtualProtect(&vtable[MFX_BY_CUSTOM_SLOT], sizeof(void *), PAGE_READWRITE, &old)) {
        note("impact: material effects table is not writable; hook not installed");
        return 0;
    }
    InterlockedExchangePointer(&vtable[MFX_BY_CUSTOM_SLOT], (void *)impact_effect);
    VirtualProtect(&vtable[MFX_BY_CUSTOM_SLOT], sizeof(void *), old, &old);
    char line[128];
    snprintf(line, sizeof(line), "impact: M4 bullet hits use %s effects, table at WHGame+0x%x", M4_LIBRARY, MFX_VTABLE_RVA);
    note(line);
    return 1;
}

DWORD WINAPI impact_hook(LPVOID unused) {
    (void)unused;
    HMODULE game;
    while (!(game = GetModuleHandleA("WHGame.dll"))) Sleep(1);
    impact_install((unsigned char *)game);
    return 0;
}
