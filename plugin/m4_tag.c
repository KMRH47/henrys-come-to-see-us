#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

void note(const char *text);
unsigned char *near_page(unsigned char *base);
unsigned char *emit_jump(unsigned char *at, void *target);
void put_jump(unsigned char *from, unsigned char *to, size_t length);

#define SET_TAG_RVA 0x4ac88c
#define GET_TAG_ID_RVA 0x78bbf8
#define PLAYER_TAG_ID 257
#define ANCHOR_LIMIT 8
#define ANCHOR_FRESH_MS 5000
#define PROBED_LIMIT 16
#define AIM_GAP_MS 250

static const unsigned char SET_TAG_BYTES[] = {
    0x45, 0x8a, 0xd1, 0x4c, 0x8b, 0xd9, 0x45, 0x85, 0xc0, 0x78, 0x7c, 0x48, 0x8b, 0x49, 0x08, 0x44,
    0x3b, 0x41, 0xfc, 0x7d, 0x72, 0x49, 0x8b, 0x43, 0x18, 0x4d, 0x63, 0xc0, 0x4e, 0x8d, 0x0c, 0x40
};
#define SEEN_SLOTS 4096
#define SEEN_LINES 3000

typedef int (*get_tag_id_fn)(void *def, const char *name);

static unsigned char *game;
static volatile LONG64 seen[SEEN_SLOTS];
static volatile LONG seen_lines;
extern volatile LONG tag_armed;

static void *volatile male_def;
static volatile LONG m4a1_id = -1;
static volatile LONG m4aim_id = -1;
static volatile LONG m4hold_id = -1;
static volatile LONG charge_id = -1;
static volatile LONG stealth_id = -1;
static volatile LONG horse_id = -1;
static volatile LONG next_m4;
static volatile LONG on_back;
static volatile LONG m4next_id = -1;
static void *volatile probed[PROBED_LIMIT];
static volatile LONG probed_count;

static void *volatile anchor_def[ANCHOR_LIMIT];
static unsigned char *volatile anchor_state[ANCHOR_LIMIT];
static volatile LONG64 anchor_seen[ANCHOR_LIMIT];
static volatile LONG64 anchor_charged[ANCHOR_LIMIT];
static volatile LONG anchor_count;

static volatile LONG inject_logged;
static volatile LONG missing_logged;
static volatile LONG lost_count;
static volatile LONG lost_lines;
static volatile LONG frame_writes;
static volatile LONG aim_lost;
static volatile LONG trace_lines;
static int traced_bits[ANCHOR_LIMIT];

double m4_since_shot_ms(void);
int m4_aim_held(void);

static void log_tag(void *caller, unsigned char *def, unsigned char *state, int id, unsigned char value) {
    LONG64 key = ((LONG64)((unsigned char *)caller - game) << 20) ^ ((LONG64)id << 1) ^ (value != 0) ^ ((LONG64)(ULONG_PTR)def << 36);
    if (!key) key = 1;
    for (unsigned slot = (unsigned)(key * 0x9e3779b97f4a7c15ULL >> 52), probe = 0; probe < 16; ++probe, slot = (slot + 1) % SEEN_SLOTS) {
        LONG64 held = InterlockedCompareExchange64(&seen[slot], key, 0);
        if (held == key) return;
        if (held == 0) break;
    }
    if (InterlockedIncrement(&seen_lines) > SEEN_LINES) return;
    unsigned char *entries = *(unsigned char **)(def + 8);
    const char *name = *(const char **)(entries + (size_t)id * 32 + 8);
    char line[256];
    snprintf(line, sizeof(line), "tag: %s=%d id %d caller WHGame+0x%zx def %p state %p",
             name ? name : "?", value != 0, id, (size_t)((unsigned char *)caller - game), (void *)def, (void *)state);
    note(line);
}

static void write_bit(unsigned char *def, unsigned char *state, int id, int value) {
    unsigned char *entries = *(unsigned char **)(def + 8);
    unsigned char *pair = *(unsigned char **)(def + 0x18) + (size_t)id * 2;
    int redirect = *(int *)(entries + (size_t)id * 32 + 4);
    if (redirect != -1) {
        unsigned char *group = *(unsigned char **)(def + 0x20) + (size_t)redirect * 2;
        if (((state[group[0]] & group[1]) == pair[1]) == (value != 0)) return;
        state[group[0]] &= (unsigned char)~group[1];
        if (value) state[pair[0]] |= pair[1];
        return;
    }
    if (value) state[pair[0]] |= pair[1];
    else state[pair[0]] &= (unsigned char)~pair[1];
}

static int read_bit(unsigned char *def, unsigned char *state, int id) {
    unsigned char *entries = *(unsigned char **)(def + 8);
    unsigned char *pair = *(unsigned char **)(def + 0x18) + (size_t)id * 2;
    int redirect = *(int *)(entries + (size_t)id * 32 + 4);
    if (redirect != -1) {
        unsigned char *group = *(unsigned char **)(def + 0x20) + (size_t)redirect * 2;
        return (state[group[0]] & group[1]) == pair[1];
    }
    return (state[pair[0]] & pair[1]) == pair[1];
}

static int anchor_index(void *def, unsigned char *state) {
    for (LONG i = 0; i < ANCHOR_LIMIT; ++i)
        if (anchor_def[i] == def && anchor_state[i] == state) return (int)i;
    return -1;
}

static void remember_anchor(void *def, unsigned char *state) {
    LONG64 now = (LONG64)GetTickCount64();
    int slot = anchor_index(def, state);
    if (slot >= 0) {
        anchor_seen[slot] = now;
        return;
    }
    LONG claimed = InterlockedIncrement(&anchor_count);
    if (claimed <= ANCHOR_LIMIT) {
        slot = (int)claimed - 1;
        anchor_state[slot] = state;
        anchor_def[slot] = def;
        anchor_charged[slot] = 0;
        anchor_seen[slot] = now;
        char line[96];
        snprintf(line, sizeof(line), "tag: player anchor %d at state %p", slot, (void *)state);
        note(line);
        return;
    }
    InterlockedDecrement(&anchor_count);
    int stalest = -1;
    LONG64 oldest = now;
    for (LONG i = 0; i < ANCHOR_LIMIT; ++i) {
        LONG64 seen_at = anchor_seen[i];
        if (seen_at == 0) { stalest = (int)i; oldest = 0; break; }
        if (seen_at < oldest) { oldest = seen_at; stalest = (int)i; }
    }
    if (stalest < 0 || (oldest != 0 && now - oldest < ANCHOR_FRESH_MS)) return;
    anchor_seen[stalest] = 0;
    anchor_charged[stalest] = 0;
    anchor_state[stalest] = state;
    anchor_def[stalest] = def;
    anchor_seen[stalest] = now;
}


static int def_probed(void *def) {
    for (LONG i = 0; i < probed_count && i < PROBED_LIMIT; ++i)
        if (probed[i] == def) return 1;
    return 0;
}

static void resolve_male_def(void *def) {
    if (male_def || def_probed(def)) return;
    if (probed_count >= PROBED_LIMIT) {
        if (InterlockedExchange(&missing_logged, 1) == 0)
            note("tag: m4a1 is not in the registry; injection idle");
        return;
    }
    probed[probed_count++] = def;
    int id = ((get_tag_id_fn)(game + GET_TAG_ID_RVA))(def, "m4a1");
    if (id < 0) return;
    m4a1_id = id;
    m4aim_id = ((get_tag_id_fn)(game + GET_TAG_ID_RVA))(def, "m4aim");
    charge_id = ((get_tag_id_fn)(game + GET_TAG_ID_RVA))(def, "charge");
    stealth_id = ((get_tag_id_fn)(game + GET_TAG_ID_RVA))(def, "stealth");
    horse_id = ((get_tag_id_fn)(game + GET_TAG_ID_RVA))(def, "horse");
    m4hold_id = ((get_tag_id_fn)(game + GET_TAG_ID_RVA))(def, "m4hold");
    m4next_id = ((get_tag_id_fn)(game + GET_TAG_ID_RVA))(def, "m4next");
    male_def = def;
    char line[192];
    snprintf(line, sizeof(line), "tag: m4a1 id %d, m4aim id %ld, m4hold id %ld, charge id %ld, stealth id %ld, horse id %ld on def %p; injection live",
             id, (long)m4aim_id, (long)m4hold_id, (long)charge_id, (long)stealth_id, (long)horse_id, def);
    note(line);
}

// Each shot clears charge for about 70 ms, and the Motion fragments keyed on it
// swapped the aim walk and idle for the loaded ones and back at every shot: the
// gait restarted and the hips jerked 17 deg, and the torso turn chased them
// (logger 2026-09-23). m4aim follows charge but holds through those gaps while
// the aim is held, and the M4's own Motion rows key on it instead.
static void write_aim(unsigned char *state, int slot, int armed) {
    if (m4aim_id < 0 || charge_id < 0) return;
    LONG64 now = (LONG64)GetTickCount64();
    int charged = read_bit(male_def, state, (int)charge_id);
    if (charged) anchor_charged[slot] = now;
    int held = charged || (anchor_charged[slot] && now - anchor_charged[slot] < AIM_GAP_MS && m4_aim_held());
    int crouched = stealth_id >= 0 && read_bit(male_def, state, (int)stealth_id);
    write_bit(male_def, state, (int)m4aim_id, armed && held && !crouched);
}

static void write_hold(unsigned char *state, int armed) {
    if (m4hold_id >= 0) write_bit(male_def, state, (int)m4hold_id, armed);
}

static void write_next(unsigned char *state) {
    if (m4next_id >= 0) write_bit(male_def, state, (int)m4next_id, next_m4 != 0);
}

void m4_tag_draw(int m4) {
    if (InterlockedExchange(&next_m4, m4) != m4)
        note(m4 ? "tag: the next draw is the M4; m4next set" : "tag: the next draw is another rifle; m4next cleared");
    if (!male_def) return;
    LONG64 now = (LONG64)GetTickCount64();
    for (LONG i = 0; i < ANCHOR_LIMIT; ++i) {
        LONG64 seen_at = anchor_seen[i];
        if (seen_at == 0 || now - seen_at > ANCHOR_FRESH_MS || anchor_def[i] != male_def) continue;
        write_next(anchor_state[i]);
    }
}

static void track(void *def, unsigned char *state, int id, unsigned char value) {
    int player = id == PLAYER_TAG_ID && value;
    if (!male_def && (tag_armed || player)) resolve_male_def(def);
    if (player && (tag_armed || def == male_def)) remember_anchor(def, state);
    if (!male_def) return;
    if (def != male_def) return;
    int slot = anchor_index(def, state);
    if (slot < 0) return;
    anchor_seen[slot] = (LONG64)GetTickCount64();
    int armed = tag_armed != 0;
    write_bit(def, state, (int)m4a1_id, armed || on_back);
    write_hold(state, armed);
    write_next(state);
    if (id == charge_id) write_aim(state, slot, armed);
    if (armed && InterlockedExchange(&inject_logged, 1) == 0)
        note("tag: m4a1 set on the player tag state");
}

static void __attribute__((noinline)) set_tag(unsigned char *def, unsigned char *state, int id, unsigned char value) {
    if (id < 0) return;
    unsigned char *entries = *(unsigned char **)(def + 8);
    if (id >= *(int *)(entries - 4)) return;
    if (tag_armed) log_tag(__builtin_return_address(0), def, state, id, value);
    write_bit(def, state, id, value);
    track(def, state, id, value);
    if (def == male_def && id == charge_id && m4_aim_held() && InterlockedIncrement(&trace_lines) <= 400) {
        char line[160];
        snprintf(line, sizeof(line), "trace: charge=%d state %p anchor %d m4aim %d, %.0f ms after the shot", value != 0,
                 (void *)state, anchor_index(def, state), m4aim_id >= 0 && read_bit(def, state, (int)m4aim_id), m4_since_shot_ms());
        note(line);
    }
}

void m4_tag_frame(void) {
    if (!male_def || m4a1_id < 0) return;
    LONG64 now = (LONG64)GetTickCount64();
    int armed = tag_armed != 0;
    for (LONG i = 0; i < ANCHOR_LIMIT; ++i) {
        LONG64 seen_at = anchor_seen[i];
        if (seen_at == 0 || now - seen_at > ANCHOR_FRESH_MS) continue;
        if (anchor_def[i] != male_def) continue;
        if (armed && !read_bit(male_def, anchor_state[i], (int)m4a1_id)) {
            InterlockedIncrement(&lost_count);
            if (InterlockedIncrement(&lost_lines) <= 40) {
                char line[160];
                snprintf(line, sizeof(line), "tag: m4a1 missing at frame on anchor %ld state %p, %.0f ms after the last shot",
                         (long)i, (void *)anchor_state[i], m4_since_shot_ms());
                note(line);
            }
        }
        if (armed) InterlockedIncrement(&frame_writes);
        int bits = read_bit(male_def, anchor_state[i], (int)charge_id) | (m4aim_id >= 0 ? read_bit(male_def, anchor_state[i], (int)m4aim_id) << 1 : 0);
        if (m4_aim_held() && bits != traced_bits[i] && InterlockedIncrement(&trace_lines) <= 400) {
            char line[160];
            snprintf(line, sizeof(line), "trace: frame anchor %ld state %p charge %d m4aim %d, %.0f ms after the shot", (long)i,
                     (void *)anchor_state[i], bits & 1, bits >> 1, m4_since_shot_ms());
            note(line);
        }
        if (armed && m4aim_id >= 0 && m4_aim_held() && anchor_charged[i] && !(stealth_id >= 0 && read_bit(male_def, anchor_state[i], (int)stealth_id)) && now - anchor_charged[i] < AIM_GAP_MS &&
            !read_bit(male_def, anchor_state[i], (int)m4aim_id))
            InterlockedIncrement(&aim_lost);
        write_bit(male_def, anchor_state[i], (int)m4a1_id, armed);
        write_aim(anchor_state[i], (int)i, armed);
        write_hold(anchor_state[i], armed);
        traced_bits[i] = read_bit(male_def, anchor_state[i], (int)charge_id) | (m4aim_id >= 0 ? read_bit(male_def, anchor_state[i], (int)m4aim_id) << 1 : 0);
    }
}

int m4_stance(void) {
    if (!male_def) return 0;
    LONG64 now = (LONG64)GetTickCount64();
    int stance = 0;
    for (LONG i = 0; i < ANCHOR_LIMIT; ++i) {
        LONG64 seen_at = anchor_seen[i];
        if (seen_at == 0 || now - seen_at > ANCHOR_FRESH_MS || anchor_def[i] != male_def) continue;
        stance |= (stealth_id >= 0 && read_bit(male_def, anchor_state[i], (int)stealth_id)) |
                  (horse_id >= 0 && read_bit(male_def, anchor_state[i], (int)horse_id)) << 1;
    }
    return stance;
}

void m4_tag_drawn(int m4) {
    m4_tag_draw(m4);
    if (m4 || !InterlockedExchange(&on_back, 0) || !male_def || m4a1_id < 0) return;
    note("tag: another rifle is drawn; m4a1 released");
    LONG64 now = (LONG64)GetTickCount64();
    for (LONG i = 0; i < ANCHOR_LIMIT; ++i) {
        LONG64 seen_at = anchor_seen[i];
        if (seen_at == 0 || now - seen_at > ANCHOR_FRESH_MS || anchor_def[i] != male_def) continue;
        write_bit(male_def, anchor_state[i], (int)m4a1_id, tag_armed);
    }
}

void m4_tag_disarm(void) {
    InterlockedExchange(&on_back, 1);
    if (!male_def || m4a1_id < 0) return;
    LONG64 now = (LONG64)GetTickCount64();
    for (LONG i = 0; i < ANCHOR_LIMIT; ++i) {
        LONG64 seen_at = anchor_seen[i];
        if (seen_at == 0 || now - seen_at > ANCHOR_FRESH_MS) continue;
        if (anchor_def[i] != male_def) continue;
        write_bit(male_def, anchor_state[i], (int)m4a1_id, 1);
        if (m4aim_id >= 0) write_bit(male_def, anchor_state[i], (int)m4aim_id, 0);
        write_hold(anchor_state[i], 0);
        anchor_charged[i] = 0;
    }
    char line[160];
    snprintf(line, sizeof(line), "tag: this draw re-set m4a1 %ld times, found it missing %ld times, m4aim missing %ld times, %ld anchors",
             (long)InterlockedExchange(&frame_writes, 0), (long)InterlockedExchange(&lost_count, 0),
             (long)InterlockedExchange(&aim_lost, 0), (long)anchor_count);
    note(line);
    InterlockedExchange(&lost_lines, 0);
}

DWORD WINAPI tag_hook(LPVOID unused) {
    (void)unused;
    HMODULE module;
    while (!(module = GetModuleHandleA("WHGame.dll"))) Sleep(1);
    game = (unsigned char *)module;
    unsigned char *site = game + SET_TAG_RVA;
    if (memcmp(site, SET_TAG_BYTES, sizeof(SET_TAG_BYTES))) {
        note("tag: WHGame.dll does not match this build's SetTag; probe not installed");
        return 0;
    }
    unsigned char *entry = near_page(game);
    if (!entry) {
        note("tag: no page near WHGame.dll; probe not installed");
        return 0;
    }
    emit_jump(entry, (void *)set_tag);
    put_jump(site, entry, 5);
    note("tag: SetTag probe at WHGame+0x4ac88c installed");
    return 0;
}
