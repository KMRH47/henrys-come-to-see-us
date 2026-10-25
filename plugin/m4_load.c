#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include "m4_weapon.h"

void note(const char *text);
unsigned char *near_page(unsigned char *base);
unsigned char *emit_jump(unsigned char *at, void *target);
void put_call(unsigned char *from, unsigned char *to, size_t length);
unsigned char *m4_item_record(void *item);

#define LOAD_CHECK_CALL_RVA 0xc5fb0f
static const unsigned char LOAD_CHECK_CALL_BYTES[] = {0xe8, 0x6c, 0xf7, 0xff, 0xff, 0x84, 0xc0};
#define LOAD_CHECK_RVA 0xc5f280
static const unsigned char LOAD_CHECK_BYTES[] = {
    0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
    0x48, 0x81, 0xec, 0x40, 0x01, 0x00, 0x00
};

static unsigned char (*load_original)(unsigned char *);
static LONG load_announced;

static unsigned char load_allowed(unsigned char *expansion) {
    if (load_original(expansion)) return 1;
    unsigned char *item = *(unsigned char **)(expansion + 0x40);
    if (!item || !m4_is_weapon_record(m4_item_record(item))) return 0;
    if (!InterlockedExchange(&load_announced, 1))
        note("load: M4 recharge allowed in a stance that refuses the load");
    return 1;
}

static int load_install(unsigned char *game) {
    if (memcmp(game + LOAD_CHECK_CALL_RVA, LOAD_CHECK_CALL_BYTES, sizeof(LOAD_CHECK_CALL_BYTES)) ||
        memcmp(game + LOAD_CHECK_RVA, LOAD_CHECK_BYTES, sizeof(LOAD_CHECK_BYTES))) {
        note("load: signature mismatch; no load hook installed");
        return 0;
    }
    unsigned char *entry = near_page(game);
    if (!entry) {
        note("load: no nearby page; no load hook installed");
        return 0;
    }
    load_original = (void *)(game + LOAD_CHECK_RVA);
    emit_jump(entry, (void *)load_allowed);
    FlushInstructionCache(GetCurrentProcess(), entry, 14);
    put_call(game + LOAD_CHECK_CALL_RVA, entry, 5);
    note("load: M4 recharge check hook installed");
    return 1;
}

DWORD WINAPI load_hook(LPVOID unused) {
    (void)unused;
    HMODULE game;
    while (!(game = GetModuleHandleA("WHGame.dll"))) Sleep(1);
    load_install((unsigned char *)game);
    return 0;
}
