#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

void note(const char *text);
unsigned char *near_page(unsigned char *base);
unsigned char *emit_jump(unsigned char *at, void *target);
void put_call(unsigned char *from, unsigned char *to, size_t length);
void put_jump(unsigned char *from, unsigned char *to, size_t length);
int m4_console_run(const char *line);

#define SIGHT_ITEM_RECORD_RVA 0x4695b4
#define SIGHT_CATEGORY_RVA 0x8f7130
#define SIGHT_LIST_CALL_RVA 0x8f689b
#define SIGHT_INFO_CALL_RVA 0x2b8aae9
#define SIGHT_OTHER_WEAPON 5
#define SIGHT_USE_RVA 0x1f493e0
#define SIGHT_USE_JUMP_RVA 0x1f4eac4
#define SIGHT_TARGET_RVA 0x2aff17c
#define SIGHT_TARGET_CALL_RVA 0x55fbbf
static const unsigned char SIGHT_CATEGORY_BYTES[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0x8b, 0x01, 0xba, 0x1b};
static const unsigned char SIGHT_USE_BYTES[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x55, 0x57, 0x41, 0x56,
    0x48, 0x8d, 0x6c, 0x24, 0xb9, 0x48, 0x81, 0xec, 0xe0, 0x00, 0x00, 0x00, 0x48, 0x8b, 0x02};
static const unsigned char SIGHT_GUNS[][16] = {
    {0x19, 0x35, 0x6b, 0x10, 0x07, 0x06, 0x29, 0x4b, 0xb7, 0xa2, 0x9d, 0x8f, 0xb5, 0x54, 0xa2, 0x3e},
    {0xf2, 0x77, 0x36, 0x35, 0x74, 0x3b, 0x02, 0x4e, 0x94, 0xaa, 0xbe, 0x00, 0xe1, 0x58, 0x83, 0xb6}
};
static const struct { unsigned char guid[16]; const char *apply; } SIGHTS[] = {
    {{0x3a, 0x5e, 0x1d, 0x7c, 0x8f, 0x2b, 0x6d, 0x4c, 0x9e, 0x41, 0xa3, 0x5f, 0x0b, 0x7d, 0x2c, 0x18}, "m4_sight_apply dot"},
    {{0x09, 0x23, 0xec, 0xbc, 0xa9, 0x15, 0xcc, 0x45, 0xba, 0x3a, 0xc9, 0x65, 0xce, 0x42, 0x72, 0x50}, "m4_sight_apply irons"}
};

static unsigned char *(*sight_record)(void *);
static int (*sight_category_original)(unsigned char *);
static void (*sight_use_original)(void *, void ***);
volatile LONG m4_sight_picking;
static int (*sight_target_original)(void *, void *);

static const char *sight_apply_line(const unsigned char *record) {
    for (size_t i = 0; record && i < sizeof(SIGHTS) / sizeof(SIGHTS[0]); i++)
        if (!memcmp(record + 8, SIGHTS[i].guid, 16)) return SIGHTS[i].apply;
    return NULL;
}

static int sight_category(unsigned char *record) {
    return sight_apply_line(record) ? SIGHT_OTHER_WEAPON : sight_category_original(record);
}

static int sight_gun(const unsigned char *record) {
    for (size_t i = 0; record && i < sizeof(SIGHT_GUNS) / sizeof(SIGHT_GUNS[0]); i++)
        if (!memcmp(record + 8, SIGHT_GUNS[i], 16)) return 1;
    return 0;
}

static void sight_use(void *inventory, void ***items) {
    unsigned char *record = items && *items && **items ? sight_record(**items) : NULL;
    const char *apply = sight_apply_line(record);
    int gun = sight_gun(record);
    char line[64];
    snprintf(line, sizeof(line), "sight: use %s, picking %ld", apply ? "sight" : gun ? "M4" : "other item", m4_sight_picking);
    note(line);
    if (apply && m4_console_run(apply)) return;
    if (gun && InterlockedExchange(&m4_sight_picking, 0)) {
        note("sight: the picker's use of the M4 is swallowed");
        return;
    }
    sight_use_original(inventory, items);
}

static int sight_target(void *inventory, void *item) {
    return item && sight_apply_line(sight_record(item)) ? 1 : sight_target_original(inventory, item);
}

static int sight_calls(unsigned char *game, unsigned int call_rva, unsigned int target_rva) {
    unsigned char *call = game + call_rva;
    return call[0] == 0xe8 && call + 5 + *(int *)(call + 1) == game + target_rva;
}

static int sight_redirect(unsigned char *game, const unsigned int *calls, size_t count, void *hook) {
    unsigned char *entry = near_page(game);
    if (!entry) return 0;
    emit_jump(entry, hook);
    FlushInstructionCache(GetCurrentProcess(), entry, 14);
    for (size_t i = 0; i < count; i++) put_call(game + calls[i], entry, 5);
    return 1;
}

static void sight_install(unsigned char *game) {
    static const unsigned int category_calls[] = {SIGHT_LIST_CALL_RVA, SIGHT_INFO_CALL_RVA};
    sight_record = (void *)(game + SIGHT_ITEM_RECORD_RVA);
    if (memcmp(game + SIGHT_CATEGORY_RVA, SIGHT_CATEGORY_BYTES, sizeof(SIGHT_CATEGORY_BYTES)) ||
        !sight_calls(game, SIGHT_LIST_CALL_RVA, SIGHT_CATEGORY_RVA) || !sight_calls(game, SIGHT_INFO_CALL_RVA, SIGHT_CATEGORY_RVA)) {
        note("sight: tab signature mismatch; the sights stay in their vanilla tab");
    } else {
        sight_category_original = (void *)(game + SIGHT_CATEGORY_RVA);
        note(sight_redirect(game, category_calls, 2, (void *)sight_category)
             ? "sight: the red dot and iron sights list under Weapons" : "sight: no nearby page; the sights stay in their vanilla tab");
    }
    static const unsigned int target_calls[] = {SIGHT_TARGET_CALL_RVA};
    if (!sight_calls(game, SIGHT_TARGET_CALL_RVA, SIGHT_TARGET_RVA)) {
        note("sight: target check signature mismatch; Apply on a sight needs a poisonable weapon");
    } else {
        sight_target_original = (void *)(game + SIGHT_TARGET_RVA);
        note(sight_redirect(game, target_calls, 1, (void *)sight_target)
             ? "sight: Apply on a sight never asks for a poisonable weapon" : "sight: no nearby page; Apply on a sight needs a poisonable weapon");
    }
    unsigned char *jump = game + SIGHT_USE_JUMP_RVA, *entry;
    if (memcmp(game + SIGHT_USE_RVA, SIGHT_USE_BYTES, sizeof(SIGHT_USE_BYTES)) ||
        jump[0] != 0xe9 || jump + 5 + *(int *)(jump + 1) != game + SIGHT_USE_RVA) {
        note("sight: use signature mismatch; applying a sight shows the poison dialog first");
        return;
    }
    if (!(entry = near_page(game))) {
        note("sight: no nearby page; applying a sight shows the poison dialog first");
        return;
    }
    sight_use_original = (void *)(game + SIGHT_USE_RVA);
    emit_jump(entry, (void *)sight_use);
    FlushInstructionCache(GetCurrentProcess(), entry, 14);
    put_jump(jump, entry, 5);
    note("sight: using a sight skips the poison flow and opens the rifle picker");
}

DWORD WINAPI sight_hook(LPVOID unused) {
    (void)unused;
    HMODULE game;
    while (!(game = GetModuleHandleA("WHGame.dll"))) Sleep(1);
    sight_install((unsigned char *)game);
    return 0;
}
