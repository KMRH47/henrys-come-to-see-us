#include "m4_look.c"
#include <stdio.h>
#include <stdlib.h>

#define require(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

#define GAME_SIZE 0x5400000
#define LOOK_CVAR_RVA 0x53554b4
#define LOOK_SKIP_RVA 0x4bce34
#define CONTINUED 0x41
#define SKIPPED 0x42

volatile LONG m4_look_off;
void *volatile m4_body_instance;

void note(const char *text) { puts(text); }

static unsigned char player[0x40], npc[0x40];

static const struct {
    const char *name;
    LONG cvar, off;
    void *instance, *body;
    int expected;
} look_cases[] = {
    {"the player with the M4 in hand skips look IK", 1, 1, player, player, SKIPPED},
    {"an NPC keeps look IK while the M4 is in hand", 1, 1, npc, player, CONTINUED},
    {"the player keeps look IK without the M4", 1, 0, player, player, CONTINUED},
    {"an NPC keeps look IK without the M4", 1, 0, npc, player, CONTINUED},
    {"an unseen body keeps look IK for everyone", 1, 1, player, NULL, CONTINUED},
    {"a missing instance keeps look IK", 1, 1, NULL, NULL, CONTINUED},
    {"ca_UseLookIK 0 still skips an NPC", 0, 0, npc, player, SKIPPED},
    {"ca_UseLookIK 0 still skips the player", 0, 1, player, player, SKIPPED},
};

static const unsigned char BRIDGE_PARAMS_IN_R13_R11_0X40[] = {
    0x41, 0x55, 0x41, 0xbb, 0x40, 0x00, 0x00, 0x00, 0x49, 0x89, 0xcd, 0x48, 0x83, 0xec, 0x20
};
static const unsigned char CONTINUE_RETURNS_R11_PLUS_1[] = {0x41, 0x8d, 0x43, 0x01, 0x48, 0x83, 0xc4, 0x20, 0x41, 0x5d, 0xc3};
static const unsigned char SKIP_RETURNS_R11_PLUS_2[] = {0x41, 0x8d, 0x43, 0x02, 0x48, 0x83, 0xc4, 0x20, 0x41, 0x5d, 0xc3};

int main(void) {
    unsigned char *game = VirtualAlloc(NULL, GAME_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    require(game);
    unsigned char *site = game + LOOK_GATE_RVA;
    memcpy(site, LOOK_GATE_BYTES, sizeof(LOOK_GATE_BYTES));
    for (size_t i = 0; i < sizeof(LOOK_GATE_BYTES); i++) {
        site[i] ^= 1;
        require(!look_install(game));
        site[i] ^= 1;
        require(!memcmp(site, LOOK_GATE_BYTES, sizeof(LOOK_GATE_BYTES)));
    }
    puts("PASS: a mismatch in any of the 17 gate bytes installs nothing and leaves the gate alone");

    require(look_install(game));
    require(look_cvar == (volatile LONG *)(game + LOOK_CVAR_RVA));
    require(site[0] == 0xe9);
    for (size_t i = 5; i < LOOK_GATE_LENGTH; i++) require(site[i] == 0x90);
    unsigned char *entry = site + 5 + *(INT32 *)(site + 1);
    require(!memcmp(entry, SAVE_VOLATILES_IN_0XC0, sizeof(SAVE_VOLATILES_IN_0XC0)));
    puts("PASS: the gate jumps to a stub that reads ca_UseLookIK from the cmp's own address");

    memcpy(site - sizeof(BRIDGE_PARAMS_IN_R13_R11_0X40), BRIDGE_PARAMS_IN_R13_R11_0X40, sizeof(BRIDGE_PARAMS_IN_R13_R11_0X40));
    memcpy(site + LOOK_GATE_LENGTH, CONTINUE_RETURNS_R11_PLUS_1, sizeof(CONTINUE_RETURNS_R11_PLUS_1));
    memcpy(game + LOOK_SKIP_RVA, SKIP_RETURNS_R11_PLUS_2, sizeof(SKIP_RETURNS_R11_PLUS_2));
    int (*bridge)(void *const *) = (void *)(site - sizeof(BRIDGE_PARAMS_IN_R13_R11_0X40));
    for (size_t i = 0; i < sizeof(look_cases) / sizeof(look_cases[0]); i++) {
        *(volatile LONG *)(game + LOOK_CVAR_RVA) = look_cases[i].cvar;
        m4_look_off = look_cases[i].off;
        m4_body_instance = look_cases[i].body;
        void *params[4] = {look_cases[i].instance, NULL, NULL, NULL};
        int result = bridge(params);
        if (result != look_cases[i].expected) {
            fprintf(stderr, "FAIL: %s returned 0x%x, want 0x%x\n", look_cases[i].name, result, look_cases[i].expected);
            exit(1);
        }
    }
    puts("PASS: only the player's instance skips look IK while the M4 is in hand; NPCs keep it, ca_UseLookIK 0 still skips all, r11 survives");
    VirtualFree(game, 0, MEM_RELEASE);
    return 0;
}
