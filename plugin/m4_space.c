#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "m4_weapon.h"

void note(const char *text);
unsigned char *near_page(unsigned char *base);
unsigned char *emit_jump(unsigned char *at, void *target);
void put_call(unsigned char *from, unsigned char *to, size_t length);

#define SPACE_DROP_CALL_RVA 0x224a32f
static const unsigned char SPACE_DROP_CALL_BYTES[] = {
    0xe8, 0x44, 0xdb, 0x85, 0x00, 0x84, 0xc0
};
#define SPACE_PRESS_CALL_RVA 0x2aaa4cd
static const unsigned char SPACE_PRESS_CALL_BYTES[] = {
    0xe8, 0xa6, 0xd9, 0xff, 0xff, 0x84, 0xc0
};
#define SPACE_REARM_CALL_RVA 0x2aa9ff0
static const unsigned char SPACE_REARM_CALL_BYTES[] = {
    0xe8, 0x83, 0xde, 0xff, 0xff, 0x84, 0xc0
};
#define SPACE_BLOCKED_RVA 0x2aa7e78
static const unsigned char SPACE_BLOCKED_BYTES[] = {
    0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x10, 0x48, 0x89, 0x78, 0x18, 0x55,
    0x48, 0x8d, 0xa8, 0x48, 0xfe, 0xff, 0xff, 0x48
};
#define SPACE_EXPANSION_RVA 0x7f3310
static const unsigned char SPACE_EXPANSION_BYTES[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48,
    0x8b, 0x01, 0xb2, 0x04, 0x48, 0x8b, 0xf1, 0xff
};
#define SPACE_ITEM_RECORD_RVA 0x4695b4
static const unsigned char SPACE_ITEM_RECORD_BYTES[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x8b, 0x15, 0xd8, 0xef, 0xc5, 0x04,
    0x48, 0x8d, 0x1d, 0x15, 0x68, 0x04, 0x05, 0x65, 0x48, 0x8b, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00,
    0x48, 0x8b, 0xf9, 0xb9, 0x18, 0x00, 0x00, 0x00, 0x48, 0x8b, 0x04, 0xd0, 0x8b, 0x04, 0x01, 0x39,
    0x05, 0x77, 0x5c, 0x17, 0x05, 0x7f, 0x18, 0x48, 0x83, 0x7f, 0x48, 0x00, 0x48, 0x0f, 0x45, 0x5f,
    0x48, 0x48, 0x8b, 0xc3, 0x48, 0x8b, 0x5c, 0x24, 0x30, 0x48, 0x83, 0xc4, 0x20, 0x5f, 0xc3
};

static const struct { size_t rva; const unsigned char *bytes; size_t length; } space_signatures[] = {
    {SPACE_DROP_CALL_RVA, SPACE_DROP_CALL_BYTES, sizeof(SPACE_DROP_CALL_BYTES)},
    {SPACE_PRESS_CALL_RVA, SPACE_PRESS_CALL_BYTES, sizeof(SPACE_PRESS_CALL_BYTES)},
    {SPACE_REARM_CALL_RVA, SPACE_REARM_CALL_BYTES, sizeof(SPACE_REARM_CALL_BYTES)},
    {SPACE_BLOCKED_RVA, SPACE_BLOCKED_BYTES, sizeof(SPACE_BLOCKED_BYTES)},
    {SPACE_EXPANSION_RVA, SPACE_EXPANSION_BYTES, sizeof(SPACE_EXPANSION_BYTES)},
    {SPACE_ITEM_RECORD_RVA, SPACE_ITEM_RECORD_BYTES, sizeof(SPACE_ITEM_RECORD_BYTES)},
};

static unsigned char (*space_original)(unsigned char *);
static unsigned char *(*space_expand)(void *);
static unsigned char *(*space_record)(void *);
static LONG space_announced;

unsigned char *m4_item_record(void *item) {
    return space_record(item);
}

static unsigned char space_blocked(unsigned char *ctrl) {
    unsigned char *actor = *(unsigned char **)(ctrl + 0x18);
    if (actor) {
        unsigned char *slot = *(unsigned char **)(actor + 0x280);
        unsigned char *expansion = slot ? space_expand(slot) : NULL;
        if (expansion) {
            unsigned char *item = *(unsigned char **)(expansion + 0x40);
            if (item && m4_is_weapon_record(space_record(item))) {
                if (!InterlockedExchange(&space_announced, 1))
                    note("space: player M4 space test bypassed");
                return 0;
            }
        }
    }
    return space_original(ctrl);
}

static int space_install(unsigned char *game) {
    for (size_t i = 0; i < sizeof(space_signatures) / sizeof(space_signatures[0]); i++) {
        if (memcmp(game + space_signatures[i].rva, space_signatures[i].bytes, space_signatures[i].length)) {
            note("space: signature mismatch; no space hooks installed");
            return 0;
        }
    }
    unsigned char *entry = near_page(game);
    if (!entry) {
        note("space: no nearby page; no space hooks installed");
        return 0;
    }
    space_original = (void *)(game + SPACE_BLOCKED_RVA);
    space_expand = (void *)(game + SPACE_EXPANSION_RVA);
    space_record = (void *)(game + SPACE_ITEM_RECORD_RVA);
    emit_jump(entry, (void *)space_blocked);
    FlushInstructionCache(GetCurrentProcess(), entry, 14);
    put_call(game + SPACE_DROP_CALL_RVA, entry, 5);
    put_call(game + SPACE_PRESS_CALL_RVA, entry, 5);
    put_call(game + SPACE_REARM_CALL_RVA, entry, 5);
    note("space: player M4 space test hooks installed");
    return 1;
}

DWORD WINAPI space_hook(LPVOID unused) {
    (void)unused;
    HMODULE game;
    while (!(game = GetModuleHandleA("WHGame.dll"))) Sleep(1);
    space_install((unsigned char *)game);
    return 0;
}
