#define WIN32_LEAN_AND_MEAN
#include "m4_weapon.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

void note(const char *text);
unsigned char *near_page(unsigned char *base);
unsigned char *emit(unsigned char *at, const void *bytes, size_t length);
unsigned char *emit_call(unsigned char *at, void *target);
unsigned char *emit_jump(unsigned char *at, void *target);
void put_jump(unsigned char *from, unsigned char *to, size_t length);
void put_call(unsigned char *from, unsigned char *to, size_t length);
void m4_tag_draw(int m4);
void m4_tag_drawn(int m4);

#define LOOKUP_RVA 0xc5ed44
#define LOOKUP_TAIL_RVA 0xc5ed5e
#define RECORD_CLASS_RVA 0x476358
#define LOOKUP_STOLEN 9
#define MASK_OFFSET 0x98
#define M4_MASK 0x01
#define AUTO_LOAD_OFFSET 0x99
#define REQUIRES_COCKING_OFFSET 0x9b
#define LOADING_MOVEMENT_OFFSET 0xe1
#define MOVEMENT_CROUCH_OFFSET 0xe2
#define GUID_OFFSET 0x8
#define AMMO_TYPE_RVA 0xc5dcfc
#define LOAD_TYPE_0_RVA 0x1e3df8e
#define LOAD_TYPE_1_RVA 0x1e8435b
#define LOAD_TYPE_2_RVA 0x1ed5540
#define LOAD_TYPE_3_RVA 0x1ed55f9
#define LOAD_TYPE_4_RVA 0x203c759
#define MASK_0_RVA 0x476236
#define MASK_1_RVA 0x4ca654
#define MASK_2_RVA 0x4cc5bf
#define MASK_3_RVA 0x940e42
#define MASK_4_RVA 0xc5dd9a
#define MASK_5_RVA 0x2d287bd
#define EQUIP_MATCH_RVA 0x1edc7b0
#define DRAW_LOOKUP_RVA 0x940e24
#define PRESS_LOOKUP_RVA 0x476325

static const unsigned char LOOKUP_BYTES[] = {0x48, 0x83, 0xec, 0x28, 0xe8, 0x0b, 0x76, 0x81, 0xff, 0x48, 0x8b, 0xc8, 0xe8};
static const unsigned char TAIL_BYTES[] = {0x48, 0x83, 0xc4, 0x28, 0xc3};
static const unsigned char RECORD_CLASS_BYTES[] = {0x48, 0x83, 0xec, 0x28, 0x48, 0x8b, 0x81, 0xe8, 0x00, 0x00, 0x00};

static const unsigned char M4_AMMO_GUIDS[][16] = {
    {0x7c, 0xda, 0x51, 0xef, 0xe7, 0x92, 0x56, 0x4e, 0x8c, 0x7f, 0x3d, 0x22, 0xa2, 0x7f, 0xe9, 0xa2},
    {0x42, 0xbb, 0x35, 0x22, 0xa1, 0x7a, 0x12, 0x4b, 0x9d, 0x7e, 0x41, 0x92, 0xbc, 0x5a, 0x2c, 0xe1}
};
static const unsigned char AMMO_TYPE_BYTES[] = {0x48, 0x83, 0xec, 0x28, 0x48, 0x8b, 0x01, 0xff, 0x50, 0x50,
    0x33, 0xc9, 0x83, 0xf8, 0x05, 0x0f, 0x4d, 0xc1, 0x48, 0x83, 0xc4, 0x28, 0xc3};
static const unsigned char LOAD_TYPE_0_BYTES[] = {0xe8, 0x69, 0xfd, 0xe1, 0xfe, 0x48, 0x8b, 0x7c, 0x24, 0x30};
static const unsigned char LOAD_TYPE_1_BYTES[] = {0xe8, 0x9c, 0x99, 0xdd, 0xfe, 0x8b, 0xc8, 0xba, 0x01, 0x00};
static const unsigned char LOAD_TYPE_2_BYTES[] = {0xe8, 0xb7, 0x87, 0xd8, 0xfe, 0x8b, 0xc8, 0xb8, 0x01, 0x00};
static const unsigned char LOAD_TYPE_3_BYTES[] = {0xe8, 0xfe, 0x86, 0xd8, 0xfe, 0x8b, 0xc8, 0xb8, 0x01, 0x00};
static const unsigned char LOAD_TYPE_4_BYTES[] = {0xe8, 0x9e, 0x15, 0xc2, 0xfe, 0x8b, 0xc8, 0xbe, 0x01, 0x00};
static const unsigned char MASK_0_BYTES[] = {0x48, 0x8b, 0x08, 0x48, 0x8b, 0x51, 0x50, 0x48, 0x8b, 0xc8, 0xff, 0xd2,
    0x4d, 0x8b, 0x85, 0x88, 0x00, 0x00, 0x00, 0x83, 0xf8, 0x05};
static const unsigned char MASK_1_BYTES[] = {0x48, 0x8b, 0x08, 0x48, 0x8b, 0x51, 0x50, 0x48, 0x8b, 0xc8, 0xff, 0xd2,
    0x83, 0xf8, 0x05};
static const unsigned char MASK_2_BYTES[] = {0x48, 0x8b, 0x08, 0x48, 0x8b, 0x51, 0x50, 0x48, 0x8b, 0xc8, 0xff, 0xd2,
    0x48, 0x8b, 0x4d, 0x97, 0x48, 0x8d, 0x55, 0x67, 0x83, 0xf8, 0x05};
static const unsigned char MASK_3_BYTES[] = {0x48, 0x8b, 0x08, 0x48, 0x8b, 0x51, 0x50, 0x48, 0x8b, 0xc8, 0xff, 0xd2,
    0x83, 0xf8, 0x05};
static const unsigned char MASK_4_BYTES[] = {0x48, 0x8b, 0xc8, 0x48, 0x8b, 0x00, 0xff, 0x50, 0x50, 0x83, 0xf8, 0x05};
static const unsigned char MASK_5_BYTES[] = {0x48, 0x8b, 0x08, 0x48, 0x8b, 0x51, 0x50, 0x48, 0x8b, 0xc8, 0xff, 0xd2,
    0x83, 0xf8, 0x05};
static const unsigned char EQUIP_MATCH_BYTES[] = {
    0x4c, 0x8b, 0x41, 0x08, 0x49, 0x8b, 0x00, 0x49, 0x3b, 0xc0, 0x74, 0x20,
    0x4c, 0x8b, 0x50, 0x10, 0x41, 0xb9, 0x01, 0x00, 0x00, 0x00, 0x8b, 0x0a,
    0x41, 0xd2, 0xe1, 0x45, 0x84, 0x8a, 0x98, 0x00, 0x00, 0x00, 0x75, 0x05,
    0x48, 0x8b, 0x00, 0xeb, 0xde, 0xb0, 0x01, 0xc3, 0x32, 0xc0, 0xc3
};
static const struct { size_t rva; const unsigned char *bytes; size_t length; } loading_calls[] = {
    {LOAD_TYPE_0_RVA, LOAD_TYPE_0_BYTES, sizeof(LOAD_TYPE_0_BYTES)},
    {LOAD_TYPE_1_RVA, LOAD_TYPE_1_BYTES, sizeof(LOAD_TYPE_1_BYTES)},
    {LOAD_TYPE_2_RVA, LOAD_TYPE_2_BYTES, sizeof(LOAD_TYPE_2_BYTES)},
    {LOAD_TYPE_3_RVA, LOAD_TYPE_3_BYTES, sizeof(LOAD_TYPE_3_BYTES)},
    {LOAD_TYPE_4_RVA, LOAD_TYPE_4_BYTES, sizeof(LOAD_TYPE_4_BYTES)}
};
static const struct { size_t rva; const unsigned char *bytes; size_t length, call, span; } mask_checks[] = {
    {MASK_0_RVA, MASK_0_BYTES, sizeof(MASK_0_BYTES), 7, 5},
    {MASK_1_RVA, MASK_1_BYTES, sizeof(MASK_1_BYTES), 7, 5},
    {MASK_2_RVA, MASK_2_BYTES, sizeof(MASK_2_BYTES), 7, 5},
    {MASK_3_RVA, MASK_3_BYTES, sizeof(MASK_3_BYTES), 7, 5},
    {MASK_4_RVA, MASK_4_BYTES, sizeof(MASK_4_BYTES), 3, 6},
    {MASK_5_RVA, MASK_5_BYTES, sizeof(MASK_5_BYTES), 7, 5}
};
#define MASK_CHECKS (sizeof(mask_checks) / sizeof(mask_checks[0]))
static unsigned char *game_base;
static int (*engine_ammo_type)(unsigned char *);
static LONG loading_announced;

int m4_is_ammo_record(const unsigned char *record) {
    for (size_t i = 0; record && i < sizeof(M4_AMMO_GUIDS) / sizeof(M4_AMMO_GUIDS[0]); i++)
        if (!memcmp(record + GUID_OFFSET, M4_AMMO_GUIDS[i], sizeof(M4_AMMO_GUIDS[i]))) return 1;
    return 0;
}

static int loading_type(unsigned char *record) {
    if (m4_is_ammo_record(record)) {
        if (!InterlockedExchange(&loading_announced, 1)) note("ammo: 5.56 uses the private loading mask; gameplay keeps its raw type");
        return 0;
    }
    return engine_ammo_type(record);
}

static int equipment_type(unsigned char *record) {
    static LONG announced;
    unsigned char *site = __builtin_return_address(0);
    int type = loading_type(record);
    int raw = engine_ammo_type(record);
    if (type != 0 || raw == 0) return type;
    for (size_t i = 0; i < MASK_CHECKS; i++) {
        if (site != game_base + mask_checks[i].rva + mask_checks[i].call + 5) continue;
        if (!(InterlockedOr(&announced, 1 << i) & (1 << i))) {
            char line[96];
            snprintf(line, sizeof(line), "ammo: 5.56 mask check WHGame+0x%zx uses the private type; raw type stays %d",
                     mask_checks[i].rva, raw);
            note(line);
        }
    }
    return type;
}

static const unsigned char SAVE_RECORD[] = {0x48, 0x89, 0x4c, 0x24, 0x08, 0x48, 0x83, 0xec, 0x28};
static const unsigned char LOAD_RULE_ARGS[] = {0x48, 0x8b, 0x4c, 0x24, 0x30, 0x48, 0x89, 0xc2, 0x4c, 0x8b, 0x44, 0x24, 0x28};

static unsigned char *m4_class;
static unsigned char table_mask;
static const size_t FLAG_OFFSETS[] = {AUTO_LOAD_OFFSET, REQUIRES_COCKING_OFFSET, LOADING_MOVEMENT_OFFSET, MOVEMENT_CROUCH_OFFSET};
static const unsigned char M4_FLAGS[] = {1, 0, 0, 1};
static const size_t LOADING_MOVEMENT_FLAG = 2;
static unsigned char table_flags[sizeof(FLAG_OFFSETS) / sizeof(FLAG_OFFSETS[0])];

static void announce(unsigned char *record, unsigned char *cls) {
    char line[128];
    snprintf(line, sizeof(line), "ammo: M4 record %p class %p table mask 0x%02x, M4 mask 0x%02x",
             record, cls, cls[MASK_OFFSET], M4_MASK);
    note(line);
}

// Rewriting the loading-on-the-move byte every frame on foot froze the aim pitch (A600),
// so the M4 sets it only in the saddle and hands it back once on leaving it.
void m4_class_flags(int m4, int mounted) {
    static LONG announced;
    static int saddle;
    if (!m4_class) return;
    for (size_t i = 0; i < sizeof(FLAG_OFFSETS) / sizeof(FLAG_OFFSETS[0]); i++) {
        if (i == LOADING_MOVEMENT_FLAG && !(m4 && mounted) && !saddle) continue;
        m4_class[FLAG_OFFSETS[i]] = m4 && (mounted || i != LOADING_MOVEMENT_FLAG) ? M4_FLAGS[i] : table_flags[i];
    }
    saddle = m4 && mounted;
    if (m4 && !InterlockedExchange(&announced, 1))
        note("ammo: M4 holds the class flags that let it load itself while it is in hand");
}

#define DRAW_CALLERS 128
#define DRAW_LINES 3000
static unsigned char *draw_caller[DRAW_CALLERS];
static unsigned char *draw_record[DRAW_CALLERS];
static ULONGLONG draw_at[DRAW_CALLERS];
static volatile LONG draw_lines;

static void trace_draw(unsigned char *record, unsigned char *caller, int m4) {
    size_t i;
    for (i = 0; i < DRAW_CALLERS && draw_caller[i] && draw_caller[i] != caller; i++) {}
    ULONGLONG now = GetTickCount64();
    if (i == DRAW_CALLERS || (draw_record[i] == record && now - draw_at[i] < 300)) return;
    draw_at[i] = now;
    draw_caller[i] = caller;
    draw_record[i] = record;
    if (InterlockedIncrement(&draw_lines) > DRAW_LINES) return;
    char line[128];
    snprintf(line, sizeof(line), "draw: caller WHGame+0x%zx record %p m4 %d", (size_t)(caller - game_base), (void *)record, m4);
    note(line);
}

#define RIFLE_RECORDS 8
static unsigned char *rifle_records[RIFLE_RECORDS];

static void remember_rifle(unsigned char *record) {
    for (size_t i = 0; i < RIFLE_RECORDS; i++) {
        if (rifle_records[i] == record) return;
        if (!rifle_records[i]) { rifle_records[i] = record; return; }
    }
}

static void on_record(unsigned char *record, unsigned char *caller) {
    if (caller < game_base || caller > game_base + 0x8000000) return;
    int m4 = m4_is_weapon_record(record);
    for (size_t i = 0; !m4 && i < RIFLE_RECORDS; i++)
        if (rifle_records[i] == record) { m4 = -1; break; }
    if (m4 && caller == game_base + PRESS_LOOKUP_RVA) m4_tag_draw(m4 > 0);
}

static const unsigned char RECORD_HOOK_ENTER[] = {0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x48, 0x8b, 0x54, 0x24, 0x20, 0x48, 0x83, 0xec, 0x28};
static const unsigned char RECORD_HOOK_LEAVE[] = {0x48, 0x83, 0xc4, 0x28, 0x41, 0x59, 0x41, 0x58, 0x5a, 0x59};

static unsigned char *on_weapon_class(unsigned char *record, unsigned char *cls, unsigned char *caller) {
    if (!cls) return cls;
    int m4 = m4_is_weapon_record(record);
    if (m4_class && (m4 || cls == m4_class)) {
        if (!m4) remember_rifle(record);
        trace_draw(record, caller, m4);
        if (caller == game_base + DRAW_LOOKUP_RVA) m4_tag_drawn(m4);
    }
    if (m4_is_weapon_record(record)) {
        if (!m4_class) {
            announce(record, cls);
            table_mask = cls[MASK_OFFSET];
            for (size_t i = 0; i < sizeof(FLAG_OFFSETS) / sizeof(FLAG_OFFSETS[0]); i++)
                table_flags[i] = cls[FLAG_OFFSETS[i]];
            m4_class = cls;
        }
        cls[MASK_OFFSET] = M4_MASK;
    } else if (cls == m4_class) {
        cls[MASK_OFFSET] = table_mask;
    }
    return cls;
}

static void install(unsigned char *game) {
    char line[160];
    unsigned char *lookup = game + LOOKUP_RVA, *tail = game + LOOKUP_TAIL_RVA;
    void *record_class = game + RECORD_CLASS_RVA, *resume = lookup + LOOKUP_STOLEN, *rule = (void *)on_weapon_class;
    if (memcmp(lookup, LOOKUP_BYTES, sizeof(LOOKUP_BYTES)) || memcmp(tail, TAIL_BYTES, sizeof(TAIL_BYTES)) ||
        memcmp(record_class, RECORD_CLASS_BYTES, sizeof(RECORD_CLASS_BYTES)) ||
        memcmp(game + AMMO_TYPE_RVA, AMMO_TYPE_BYTES, sizeof(AMMO_TYPE_BYTES)) ||
        memcmp(game + EQUIP_MATCH_RVA, EQUIP_MATCH_BYTES, sizeof(EQUIP_MATCH_BYTES))) {
        note("ammo: WHGame.dll does not match this build's class lookup; hook not installed");
        return;
    }
    for (size_t i = 0; i < sizeof(loading_calls) / sizeof(loading_calls[0]); i++) {
        if (memcmp(game + loading_calls[i].rva, loading_calls[i].bytes, loading_calls[i].length)) {
            note("ammo: loading call signature mismatch; no ammo hooks installed");
            return;
        }
    }
    for (size_t i = 0; i < MASK_CHECKS; i++) {
        if (memcmp(game + mask_checks[i].rva, mask_checks[i].bytes, mask_checks[i].length)) {
            note("ammo: mask check signature mismatch; no ammo hooks installed");
            return;
        }
    }
    unsigned char *entry = near_page(game);
    if (!entry) {
        note("ammo: no page near WHGame.dll; hook not installed");
        return;
    }
    unsigned char *at = emit(entry, SAVE_RECORD, sizeof(SAVE_RECORD));
    at = emit_call(at, record_class);
    unsigned char *exit = emit_jump(at, resume);
    at = emit(exit, LOAD_RULE_ARGS, sizeof(LOAD_RULE_ARGS));
    at = emit_call(at, rule);
    at = emit(at, TAIL_BYTES, sizeof(TAIL_BYTES));
    engine_ammo_type = (void *)(game + AMMO_TYPE_RVA);
    game_base = game;
    unsigned char *equip = emit_jump(at, (void *)loading_type);
    const unsigned char record_arg[] = {0x48, 0x8b, 0xc8};
    emit_jump(emit(equip, record_arg, sizeof(record_arg)), (void *)equipment_type);
    for (size_t i = 0; i < MASK_CHECKS; i++)
        put_call(game + mask_checks[i].rva + mask_checks[i].call, equip, mask_checks[i].span);
    for (size_t i = 0; i < sizeof(loading_calls) / sizeof(loading_calls[0]); i++)
        put_call(game + loading_calls[i].rva, at, 5);
    put_jump(tail, exit, sizeof(TAIL_BYTES));
    put_jump(lookup, entry, LOOKUP_STOLEN);
    unsigned char *probe = near_page(game);
    if (probe) {
        at = emit(probe, RECORD_HOOK_ENTER, sizeof(RECORD_HOOK_ENTER));
        at = emit_call(at, (void *)on_record);
        at = emit(at, RECORD_HOOK_LEAVE, sizeof(RECORD_HOOK_LEAVE));
        at = emit(at, RECORD_CLASS_BYTES, sizeof(RECORD_CLASS_BYTES));
        emit_jump(at, (unsigned char *)record_class + sizeof(RECORD_CLASS_BYTES));
        put_jump(record_class, probe, sizeof(RECORD_CLASS_BYTES));
        note("draw: key press probe installed at WHGame+0x476358");
    }
    snprintf(line, sizeof(line), "ammo: hook installed at WHGame+0x%x, stub %p, M4 mask 0x%02x", LOOKUP_RVA, entry, M4_MASK);
    note(line);
    note("ammo: five loading calls and six inline mask checks filtered; gameplay type untouched");
}

DWORD WINAPI ammo_hook(LPVOID unused) {
    (void)unused;
    HMODULE game;
    while (!(game = GetModuleHandleA("WHGame.dll"))) Sleep(1);
    install((unsigned char *)game);
    return 0;
}
