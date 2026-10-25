#include "m4_impact.c"
#include <stdlib.h>

#define require(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

static unsigned char mfx[0x80];
static double since_shot;
static unsigned short custom_id;
static int m4_library_loaded;

void note(const char *text) { puts(text); }
double m4_since_shot_ms(void) { return since_shot; }

static unsigned short fake_by_custom(void *self, const char *custom, int surface) {
    require(self == mfx && surface == 7);
    (void)custom;
    return custom_id;
}

static unsigned short fake_by_name(void *self, const char *library, const char *effect) {
    require(self == mfx);
    for (unsigned short i = 0; i < sizeof(IMPACT_EFFECTS) / sizeof(IMPACT_EFFECTS[0]); i++) {
        if (strcmp(effect, IMPACT_EFFECTS[i])) continue;
        if (!strcmp(library, VANILLA_LIBRARY)) return 100 + i;
        if (!strcmp(library, M4_LIBRARY)) return m4_library_loaded ? 200 + i : 0;
    }
    return 0;
}

static const struct {
    const char *name;
    const char *custom;
    double since_shot;
    unsigned short custom_id;
    int m4_library_loaded;
    unsigned short expected;
} impact_cases[] = {
    {"M4 passes into wood", "hit_passed_bullet", 200, 102, 1, 202},
    {"M4 stops in water", "hit_stopped_bullet", 1400, 114, 1, 214},
    {"M4 first effect", "hit_passed_bullet", 0, 100, 1, 200},
    {"bullet after the M4 window", "hit_passed_bullet", 1500, 102, 1, 102},
    {"arrow inside the M4 window", "hit_passed_arrow", 200, 102, 1, 102},
    {"melee inside the M4 window", "hit_stopped_sword_slash", 200, 102, 1, 102},
    {"row prefix only", "hit_passed_bullet_npc", 200, 102, 1, 102},
    {"no custom name", NULL, 200, 102, 1, 102},
    {"no effect for the surface", "hit_passed_bullet", 200, 0, 1, 0},
    {"effect from another library", "hit_passed_bullet", 200, 999, 1, 999},
    {"M4 library not loaded", "hit_stopped_bullet", 200, 105, 0, 105},
};

int main(void) {
    unsigned char *game = VirtualAlloc(NULL, 0x4100000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    require(game);
    void **vtable = (void **)(game + MFX_VTABLE_RVA);
    memcpy(game + MFX_BY_NAME_RVA, MFX_BY_NAME_BYTES, sizeof(MFX_BY_NAME_BYTES));
    memcpy(game + MFX_BY_CUSTOM_RVA, MFX_BY_CUSTOM_BYTES, sizeof(MFX_BY_CUSTOM_BYTES));
    vtable[MFX_BY_NAME_SLOT] = game + MFX_BY_NAME_RVA;
    vtable[MFX_BY_CUSTOM_SLOT] = game + MFX_BY_CUSTOM_RVA;
    unsigned char *const breaks[] = {
        (unsigned char *)&vtable[MFX_BY_NAME_SLOT], (unsigned char *)&vtable[MFX_BY_CUSTOM_SLOT],
        game + MFX_BY_NAME_RVA, game + MFX_BY_CUSTOM_RVA,
    };
    for (size_t i = 0; i < sizeof(breaks) / sizeof(breaks[0]); i++) {
        *breaks[i] ^= 1;
        require(!impact_install(game));
        *breaks[i] ^= 1;
        require(vtable[MFX_BY_CUSTOM_SLOT] == game + MFX_BY_CUSTOM_RVA);
    }
    puts("PASS: four signature failures leave the material effects table alone");

    require(impact_install(game));
    require(vtable[MFX_BY_CUSTOM_SLOT] == (void *)impact_effect);
    require(vtable[MFX_BY_NAME_SLOT] == game + MFX_BY_NAME_RVA);
    puts("PASS: install swaps only the custom-name lookup slot");

    effect_by_custom = fake_by_custom;
    effect_by_name = fake_by_name;
    unsigned short (*lookup)(void *, const char *, int) = vtable[MFX_BY_CUSTOM_SLOT];
    for (size_t i = 0; i < sizeof(impact_cases) / sizeof(impact_cases[0]); i++) {
        since_shot = impact_cases[i].since_shot;
        custom_id = impact_cases[i].custom_id;
        m4_library_loaded = impact_cases[i].m4_library_loaded;
        unsigned short got = lookup(mfx, impact_cases[i].custom, 7);
        if (got != impact_cases[i].expected) {
            fprintf(stderr, "FAIL: %s returned %u, want %u\n", impact_cases[i].name, got, impact_cases[i].expected);
            exit(1);
        }
    }
    puts("PASS: only M4 bullet rows inside the hit window move from destructibles to m4_impacts effects");
    VirtualFree(game, 0, MEM_RELEASE);
    return 0;
}
