#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include "m4_weapon.h"

void note(const char *text);
unsigned char *near_page(unsigned char *base);
unsigned char *emit_jump(unsigned char *at, void *target);
void put_jump(unsigned char *from, unsigned char *to, size_t length);
unsigned char *m4_item_record(void *item);

#define TORCH_INVOKE_JUMP_RVA 0x1f248b4
static const unsigned char TORCH_INVOKE_BYTES[] = {0x48, 0x8b, 0x49, 0x08, 0xe9, 0xef, 0x96, 0xb8, 0x00};
#define TORCH_TOGGLE_RVA 0x2aadfa8
static const unsigned char TORCH_TOGGLE_BYTES[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x41, 0x10, 0x48, 0x8b, 0xf9
};
#define TORCH_EXPANSION_RVA 0x7f3310

static void (*torch_original)(unsigned char *);
static unsigned char *(*torch_expand)(void *);
static LONG torch_announced;

static int torch_m4_in_hand(unsigned char *self) {
    unsigned char *actor = *(unsigned char **)(self + 0x10);
    unsigned char *slot = actor ? *(unsigned char **)(actor + 0x280) : NULL;
    unsigned char *expansion = slot ? torch_expand(slot) : NULL;
    unsigned char *item = expansion ? *(unsigned char **)(expansion + 0x40) : NULL;
    return item && m4_is_weapon_record(m4_item_record(item));
}

static void torch_toggle(unsigned char *self) {
    if (torch_m4_in_hand(self)) {
        if (!InterlockedExchange(&torch_announced, 1))
            note("torch: the reload key leaves the torch alone while the M4 is in hand");
        return;
    }
    torch_original(self);
}

static int torch_install(unsigned char *game) {
    if (memcmp(game + TORCH_INVOKE_JUMP_RVA - 4, TORCH_INVOKE_BYTES, sizeof(TORCH_INVOKE_BYTES)) ||
        memcmp(game + TORCH_TOGGLE_RVA, TORCH_TOGGLE_BYTES, sizeof(TORCH_TOGGLE_BYTES))) {
        note("torch: signature mismatch; no torch hook installed");
        return 0;
    }
    unsigned char *entry = near_page(game);
    if (!entry) {
        note("torch: no nearby page; no torch hook installed");
        return 0;
    }
    torch_original = (void *)(game + TORCH_TOGGLE_RVA);
    torch_expand = (void *)(game + TORCH_EXPANSION_RVA);
    emit_jump(entry, (void *)torch_toggle);
    FlushInstructionCache(GetCurrentProcess(), entry, 14);
    put_jump(game + TORCH_INVOKE_JUMP_RVA, entry, 5);
    note("torch: M4 torch key hook installed");
    return 1;
}

DWORD WINAPI torch_hook(LPVOID unused) {
    (void)unused;
    HMODULE game;
    while (!(game = GetModuleHandleA("WHGame.dll"))) Sleep(1);
    torch_install((unsigned char *)game);
    return 0;
}
