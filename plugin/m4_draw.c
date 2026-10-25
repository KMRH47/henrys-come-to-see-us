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

#define DRAW_AIM_START_RVA 0x2ace880
static const unsigned char DRAW_AIM_START_BYTES[] = {0x48, 0x8b, 0xc1, 0x48, 0x8b, 0x49, 0x50, 0x48, 0x85, 0xc9};
#define DRAW_PRESS_JUMP_RVA 0x2aaa4ea
static const unsigned char DRAW_PRESS_JUMP_BYTES[] = {0xe9, 0x91, 0x43, 0x02, 0x00};
#define DRAW_READY_CALL_RVA 0x2aaa020
static const unsigned char DRAW_READY_CALL_BYTES[] = {0xe8, 0x5b, 0x48, 0x02, 0x00};
#define DRAW_YIELD_RVA 0x14962ec
static const unsigned char DRAW_YIELD_BYTES[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x8b, 0x0a, 0x48, 0x85};
#define DRAW_YIELD_SLOT_RVA 0x3a5b9e8
#define AIM_ACTION_VTABLE_RVA 0x3e7b1b0
#define DRAW_LOGIC_VTABLE_RVA 0x3a5b7d0
#define DRAW_LOGIC_ATTACHED 0

static const struct { size_t rva; const unsigned char *bytes; size_t length; } draw_signatures[] = {
    {DRAW_AIM_START_RVA, DRAW_AIM_START_BYTES, sizeof(DRAW_AIM_START_BYTES)},
    {DRAW_PRESS_JUMP_RVA, DRAW_PRESS_JUMP_BYTES, sizeof(DRAW_PRESS_JUMP_BYTES)},
    {DRAW_READY_CALL_RVA, DRAW_READY_CALL_BYTES, sizeof(DRAW_READY_CALL_BYTES)},
    {DRAW_YIELD_RVA, DRAW_YIELD_BYTES, sizeof(DRAW_YIELD_BYTES)},
};

static unsigned char *draw_game;
static ULONG_PTR (*aim_start_original)(unsigned char *);
static unsigned char (*yield_original)(unsigned char *, unsigned char **);
static int m4_aim_starting;

static ULONG_PTR aim_start(unsigned char *expansion) {
    unsigned char *item = *(unsigned char **)(expansion + 0x40);
    m4_aim_starting = item && m4_is_weapon_record(m4_item_record(item));
    ULONG_PTR result = aim_start_original(expansion);
    m4_aim_starting = 0;
    return result;
}

static unsigned char draw_yields(unsigned char *self, unsigned char **next) {
    if (yield_original(self, next)) return 1;
    if (!m4_aim_starting || !*next || *(unsigned char **)*next != draw_game + AIM_ACTION_VTABLE_RVA) return 0;
    unsigned char *logic = *(unsigned char **)(self + 0xc8);
    if (!logic || *(unsigned char **)logic != draw_game + DRAW_LOGIC_VTABLE_RVA) return 0;
    int state = *(int *)(logic + 0x48);
    char line[96];
    snprintf(line, sizeof(line), "draw: M4 aim %s the draw, state %d", state == DRAW_LOGIC_ATTACHED ? "replaced" : "waits for", state);
    note(line);
    return state == DRAW_LOGIC_ATTACHED;
}

static int draw_install(unsigned char *game) {
    for (size_t i = 0; i < sizeof(draw_signatures) / sizeof(draw_signatures[0]); i++) {
        if (memcmp(game + draw_signatures[i].rva, draw_signatures[i].bytes, draw_signatures[i].length)) {
            note("draw: signature mismatch; no draw hooks installed");
            return 0;
        }
    }
    unsigned char **slot = (unsigned char **)(game + DRAW_YIELD_SLOT_RVA);
    if (*slot != game + DRAW_YIELD_RVA) {
        note("draw: vtable slot mismatch; no draw hooks installed");
        return 0;
    }
    unsigned char *entry = near_page(game);
    if (!entry) {
        note("draw: no nearby page; no draw hooks installed");
        return 0;
    }
    draw_game = game;
    aim_start_original = (void *)(game + DRAW_AIM_START_RVA);
    yield_original = (void *)(game + DRAW_YIELD_RVA);
    emit_jump(entry, (void *)aim_start);
    FlushInstructionCache(GetCurrentProcess(), entry, 14);
    DWORD old;
    VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old);
    *slot = (unsigned char *)draw_yields;
    VirtualProtect(slot, sizeof(*slot), old, &old);
    put_jump(game + DRAW_PRESS_JUMP_RVA, entry, 5);
    put_call(game + DRAW_READY_CALL_RVA, entry, 5);
    note("draw: M4 aim may replace an attached draw");
    return 1;
}

DWORD WINAPI draw_hook(LPVOID unused) {
    (void)unused;
    HMODULE game;
    while (!(game = GetModuleHandleA("WHGame.dll"))) Sleep(1);
    draw_install((unsigned char *)game);
    return 0;
}
