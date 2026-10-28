#include "m4_ammo.c"
#include "m4_damage.c"
#include "m4_qam.c"
#include <stdlib.h>

#define require(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

void m4_tag_draw(int m4) { (void)m4; }
void m4_tag_drawn(int m4) { (void)m4; }
static unsigned escape_taps;
volatile LONG m4_sight_dot;
volatile LONG m4_sight_picking;
void m4_tap_escape(void) { escape_taps++; }

static unsigned lines;
static char last_line[256];
void note(const char *text) {
    lines++;
    snprintf(last_line, sizeof(last_line), "%s", text);
    puts(text);
}

#define SOUL ((void *)0x5151)
#define CONTEXT ((void *)0x7171)
#define UNAWARE_STAT 0x2f

static unsigned buff_calls;
static float fake_buff(void *soul, int stat, float value, void *context) {
    require(soul == SOUL && stat == UNAWARE_STAT && context == CONTEXT);
    buff_calls++;
    return value * 3.0f;
}

static unsigned char *fake_record(void *item) {
    return *(unsigned char **)item;
}

static unsigned char *fake_item(unsigned char *hit) {
    return *(unsigned char **)(hit + HIT_WEAPON_ID);
}

#define VICTIM ((void *)0x6161)
static unsigned level_calls;
static unsigned char fake_level(void *victim) {
    require(victim == VICTIM);
    level_calls++;
    return 1;
}

#define BUFFS ((void *)0x8181)
static unsigned scale_calls;
static int scale_stat;
static float scale_factor;
static float fake_scale(void *unused, void *buffs, int stat, float value, void *context, int flag) {
    (void)unused;
    require(buffs == BUFFS && stat == scale_stat && context == CONTEXT && (flag & 0xff) == 1);
    scale_calls++;
    return value * scale_factor;
}

#define HIT_SIZE 0x64
static float fake_range;
static unsigned distance_calls;
static float fake_distance(unsigned char *hit) {
    require(hit);
    distance_calls++;
    return fake_range;
}

static unsigned modify_calls;
static int modify_flag;
static float fake_modify(unsigned char *hit, int flag, float attack) {
    require(hit);
    modify_calls++;
    modify_flag = flag;
    return attack * 2.0f;
}

static float fake_feet[3] = {7.5f, 8.25f, 30.0f};
static float *fake_world_pos(void *entity, float *out) {
    (void)entity;
    (void)out;
    return fake_feet;
}
static void *fake_entity_table[ENTITY_WORLD_POS / 8 + 1];
static void *fake_entity_object = fake_entity_table;
static void *fake_entity(void *victim, int index) {
    require(victim == VICTIM && index == 0);
    return &fake_entity_object;
}

static char cvar_text[64];
static unsigned set_calls, register_calls, get_calls, clear_calls;
static void *fake_cvar_object;
static const char *fake_missing;
static void fake_clear_flags(void *cvar, int flags) {
    require(cvar == &fake_cvar_object && flags == CVAR_CHEAT_FLAGS);
    clear_calls++;
}
static void *fake_qam_object;
static char qam_seen[32];
static void fake_set_string(void *cvar, const char *text) {
    if (cvar == &fake_qam_object) {
        snprintf(qam_seen, sizeof(qam_seen), "%s", text);
        return;
    }
    require(cvar == &fake_cvar_object);
    set_calls++;
    snprintf(cvar_text, sizeof(cvar_text), "%s", text);
}
static void *fake_register_string(void *console, const char *name, const char *value, int flags, const char *help, void *change) {
    (void)help;
    require(flags == 0 && change == NULL);
    if (!strcmp(name, QAM_CVAR)) {
        snprintf(qam_seen, sizeof(qam_seen), "%s", value);
        return &fake_qam_object;
    }
    require(!strcmp(name, HEAD_CVAR));
    register_calls++;
    snprintf(cvar_text, sizeof(cvar_text), "%s", value);
    return *(void **)console == NULL ? NULL : &fake_cvar_object;
}
static void *fake_get_cvar(void *console, const char *name) {
    (void)console;
    require(name);
    get_calls++;
    return fake_missing && !strcmp(name, fake_missing) ? NULL : &fake_cvar_object;
}

__attribute__((naked)) static float armour_bridge(unsigned char *hit, int flag, float attack, float *lane, int channel) {
    __asm__ volatile(
        "push %rdi\n push %r15\n sub $0x28, %rsp\n"
        "mov %r9, %rdi\n mov 0x60(%rsp), %r15d\n"
        "call armour_attack\n"
        "add $0x28, %rsp\n pop %r15\n pop %rdi\n ret\n");
}

static void *call_get_cvar(void *console, const char *name) {
    void *(*get)(void *, const char *) = (*(void ***)console)[CONSOLE_GET_CVAR / 8];
    return get(console, name);
}

__attribute__((naked)) static int level_bridge(unsigned char *hit, void *victim) {
    __asm__ volatile(
        "push %rdi\n sub $0x20, %rsp\n"
        "mov %rcx, %rdi\n mov %rdx, %rcx\n"
        "call level_check\n"
        "add $0x20, %rsp\n pop %rdi\n ret\n");
}

__attribute__((naked)) static float attack_buff_bridge(unsigned char *hit, void *buffs, void *context, float attack) {
    __asm__ volatile(
        "push %rdi\n sub $0x30, %rsp\n"
        "mov %rcx, %rdi\n mov %r8, 0x20(%rsp)\n movq $0x5501, 0x28(%rsp)\n"
        "call attack_buff\n"
        "add $0x30, %rsp\n pop %rdi\n ret\n");
}

__attribute__((naked)) static float health_bridge(unsigned char *hit, float *lane, void *buffs, float loss, float dmgr, int channel) {
    __asm__ volatile(
        "push %rdi\n push %r14\n sub $0xa8, %rsp\n"
        "movdqu %xmm6, 0x30(%rsp)\n"
        "movss 0xe0(%rsp), %xmm6\n mov 0xe8(%rsp), %eax\n mov %eax, 0x90(%rsp)\n"
        "mov %rcx, %rdi\n mov %rdx, %r14\n mov %r8, %rdx\n"
        "movq $0x7171, 0x20(%rsp)\n movq $0x5501, 0x28(%rsp)\n"
        "call health_buff\n"
        "movdqu 0x30(%rsp), %xmm6\n"
        "add $0xa8, %rsp\n pop %r14\n pop %rdi\n ret\n");
}

static unsigned char *head_hit;
static float *head_lane;
static DWORD WINAPI off_thread_hit(LPVOID unused) {
    (void)unused;
    health_bridge(head_hit, head_lane, BUFFS, 18.0f, 242.0f, 4);
    return 0;
}

static unsigned char *patched_call(unsigned char *game, size_t rva) {
    unsigned char *site = game + rva;
    require(site[0] == 0xe8);
    return site + 5 + *(INT32 *)(site + 1);
}

void *attack_site;
__attribute__((naked)) static float attack_bridge(float raw, float base, unsigned char *weapon, unsigned char *ammo, unsigned char *victim) {
    __asm__ volatile(
        "mov %rsp, %rax\n mov %rbx, 8(%rax)\n mov %rbp, 0x10(%rax)\n mov %rsi, 0x18(%rax)\n"
        "mov 0x28(%rax), %rbp\n push %rdi\n sub $0x80, %rsp\n"
        "movaps %xmm6, -0x18(%rax)\n movaps %xmm7, -0x28(%rax)\n movaps %xmm8, -0x38(%rax)\n"
        "mov %r8, %rsi\n mov %r9, %rdi\n movaps %xmm1, %xmm8\n"
        "jmp *attack_site(%rip)\n");
}

void *unaware_site;
__attribute__((naked)) static float unaware_bridge(unsigned char *weapon, unsigned char *ammo, float attack) {
    __asm__ volatile(
        "push %rbx\n push %r15\n sub $0x28, %rsp\n"
        "mov %rcx, %rbx\n mov %rdx, %r15\n"
        "mov $0x5151, %ecx\n mov $0x2f, %edx\n mov $0x7171, %r9d\n"
        "call *unaware_site(%rip)\n"
        "add $0x28, %rsp\n pop %r15\n pop %rbx\n ret\n");
}

enum { NONE, M4_WEAPON, OTHER_WEAPON, M4_ROUND, M4_SPARE_ROUND, HANDGONNE_ROUND, NO_RECORD };

static const struct {
    const char *name;
    int fixed, attack, weapon, ammo, victim, m4;
} damage_cases[] = {
    {"M4 round from the M4 hits an NPC", 1, 2200, M4_WEAPON, M4_ROUND, 1, 1},
    {"unloadable M4 round hits an NPC", 1, 2200, M4_WEAPON, M4_SPARE_ROUND, 1, 1},
    {"M4 round after a swap to another weapon", 1, 2200, OTHER_WEAPON, M4_ROUND, 1, 1},
    {"M4 round with no weapon left", 1, 2200, NONE, M4_ROUND, 1, 1},
    {"M4 preview with no victim", 1, 2200, M4_WEAPON, M4_ROUND, 0, 1},
    {"no ammo item, the M4 is the weapon", 1, 2200, M4_WEAPON, NONE, 1, 1},
    {"M4 round, attack tuned to 18.5", 1, 1850, M4_WEAPON, M4_ROUND, 1, 1},
    {"M4 round, attack tuned to 0 keeps the base", 1, 0, M4_WEAPON, M4_ROUND, 1, 1},
    {"handgonne ball", 1, 2200, OTHER_WEAPON, HANDGONNE_ROUND, 1, 0},
    {"handgonne ball while the M4 is in hand", 1, 2200, M4_WEAPON, HANDGONNE_ROUND, 1, 0},
    {"no ammo item, another weapon", 1, 2200, OTHER_WEAPON, NONE, 1, 0},
    {"no ammo item and no weapon", 1, 2200, NONE, NONE, 1, 0},
    {"ammo item without a record", 1, 2200, M4_WEAPON, NO_RECORD, 1, 0},
    {"weapon item without a record", 1, 2200, NO_RECORD, NONE, 1, 0},
    {"tuned off, M4 round", 0, 2200, M4_WEAPON, M4_ROUND, 1, 0},
    {"tuned off, handgonne ball", 0, 2200, OTHER_WEAPON, HANDGONNE_ROUND, 1, 0},
};

#define SMASH 4
#define HEAD 1.5f

static const struct {
    const char *name;
    long start, end, rest;
    float range, factor;
} falloff_cases[] = {
    {"point blank", 50, 150, 85, 0.0f, 1.0f},
    {"at the near range", 50, 150, 85, 50.0f, 1.0f},
    {"a quarter of the way", 50, 150, 85, 75.0f, 0.9625f},
    {"half way", 50, 150, 85, 100.0f, 0.925f},
    {"at the far range", 50, 150, 85, 150.0f, 0.85f},
    {"past the far range", 50, 150, 85, 400.0f, 0.85f},
    {"floor tuned to 50, half way", 50, 150, 50, 100.0f, 0.75f},
    {"floor tuned above 100 clamps to full", 50, 150, 140, 300.0f, 1.0f},
    {"floor tuned below 0 clamps to nothing", 50, 150, -20, 300.0f, 0.0f},
    {"far at near drops straight to the floor", 80, 80, 85, 81.0f, 0.85f},
    {"far below near, inside near", 80, 40, 85, 60.0f, 1.0f},
    {"far below near, past near", 80, 40, 85, 90.0f, 0.85f},
};

static const struct {
    const char *name;
    int m4, channel, attack_slot;
    float defense, range, attack;
} armour_cases[] = {
    {"M4 smash on a helmet, point blank", 1, SMASH, 2, 35.0f, 10.0f, 484.0f},
    {"M4 smash on a helmet, 150 m", 1, SMASH, 2, 35.0f, 150.0f, 411.4f},
    {"M4 smash on a helmet, 100 m", 1, SMASH, 2, 35.0f, 100.0f, 447.7f},
    {"M4 smash on bare flesh, 150 m", 1, SMASH, 2, 0.0f, 150.0f, 484.0f},
    {"M4 stab on armour, 150 m", 1, 1, 0, 10.0f, 150.0f, 411.4f},
    {"M4 slash on armour, 150 m", 1, 2, 1, 10.0f, 150.0f, 411.4f},
    {"M4 unknown channel, 150 m", 1, 3, 2, 35.0f, 150.0f, 484.0f},
    {"handgonne smash on a helmet, 150 m", 0, SMASH, 2, 35.0f, 150.0f, 484.0f},
};

int main(void) {
    unsigned char *game = VirtualAlloc(NULL, 0x5000000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    require(game);
    memcpy(game + ATTACK_RVA, ATTACK_BYTES, sizeof(ATTACK_BYTES));
    memcpy(game + HIT_RVA, HIT_BYTES, sizeof(HIT_BYTES));
    memcpy(game + HEALTH_RVA, HEALTH_BYTES, sizeof(HEALTH_BYTES));
    memcpy(game + AMMO_LOOKUP_RVA, AMMO_LOOKUP_BYTES, sizeof(AMMO_LOOKUP_BYTES));
    memcpy(game + CHANNEL_RVA, CHANNEL_BYTES, sizeof(CHANNEL_BYTES));
    memcpy(game + DISTANCE_RVA, DISTANCE_BYTES, sizeof(DISTANCE_BYTES));
    memcpy(game + ENTITY_RVA, ENTITY_BYTES, sizeof(ENTITY_BYTES));
    memcpy(game + CONSOLE_USE_RVA, CONSOLE_USE_BYTES, sizeof(CONSOLE_USE_BYTES));
    const size_t breaks[] = {ATTACK_RVA, ATTACK_TAIL_RVA, ATTACK_RVA + sizeof(ATTACK_BYTES) - 1,
                             HIT_RVA, UNAWARE_CALL_RVA + 1, HIT_RVA + sizeof(HIT_BYTES) - 1,
                             HEALTH_RVA, LEVEL_CALL_RVA + 1, HEALTH_BUFF_CALL_RVA + 1, ATTACK_BUFF_CALL_RVA + 1,
                             HEALTH_RVA + sizeof(HEALTH_BYTES) - 1, AMMO_LOOKUP_RVA + 0x21,
                             CHANNEL_RVA, MODIFY_CALL_RVA + 1, CHANNEL_RVA + sizeof(CHANNEL_BYTES) - 1,
                             DISTANCE_RVA, DISTANCE_RVA + sizeof(DISTANCE_BYTES) - 1, ENTITY_RVA, CONSOLE_USE_RVA + 12};
    for (size_t i = 0; i < sizeof(breaks) / sizeof(breaks[0]); i++) {
        game[breaks[i]] ^= 1;
        require(!damage_install(game));
        game[breaks[i]] ^= 1;
        require(!memcmp(game + ATTACK_RVA, ATTACK_BYTES, sizeof(ATTACK_BYTES)));
        require(!memcmp(game + HIT_RVA, HIT_BYTES, sizeof(HIT_BYTES)));
        require(!memcmp(game + HEALTH_RVA, HEALTH_BYTES, sizeof(HEALTH_BYTES)));
        require(!memcmp(game + CHANNEL_RVA, CHANNEL_BYTES, sizeof(CHANNEL_BYTES)));
    }
    puts("PASS: nineteen signature failures leave the attack function, the hit handler, the channel step and the health stage alone");

    require(damage_install(game));
    unsigned char *tail = game + ATTACK_TAIL_RVA, *call = game + UNAWARE_CALL_RVA;
    static const unsigned char JUMP[] = {0xff, 0x25, 0x00, 0x00, 0x00, 0x00};
    require(tail[0] == 0xe9 && call[0] == 0xe8);
    unsigned char *entry = tail + 5 + *(INT32 *)(tail + 1), *unaware = call + 5 + *(INT32 *)(call + 1);
    require(!memcmp(entry, JUMP, sizeof(JUMP)) && *(void **)(entry + 6) == (void *)attack_tail);
    require(!memcmp(unaware, JUMP, sizeof(JUMP)) && *(void **)(unaware + 6) == (void *)unaware_bonus);
    require(!memcmp(tail + 5, ATTACK_BYTES + (ATTACK_TAIL_RVA - ATTACK_RVA) + 5, sizeof(ATTACK_BYTES) - (ATTACK_TAIL_RVA - ATTACK_RVA) - 5));
    require(m4_attack_resume == tail + 5 && m4_buff_original == game + BUFF_RVA && damage_record == (void *)(game + ITEM_RECORD_RVA));
    const struct { size_t rva; void *stub; size_t base; const unsigned char *bytes; } calls[] = {
        {LEVEL_CALL_RVA, (void *)level_check, HEALTH_RVA, HEALTH_BYTES},
        {ATTACK_BUFF_CALL_RVA, (void *)attack_buff, HEALTH_RVA, HEALTH_BYTES},
        {HEALTH_BUFF_CALL_RVA, (void *)health_buff, HEALTH_RVA, HEALTH_BYTES},
        {MODIFY_CALL_RVA, (void *)armour_attack, CHANNEL_RVA, CHANNEL_BYTES},
    };
    for (size_t i = 0; i < sizeof(calls) / sizeof(calls[0]); i++) {
        unsigned char *to = patched_call(game, calls[i].rva);
        require(!memcmp(to, JUMP, sizeof(JUMP)) && *(void **)(to + 6) == calls[i].stub);
        require(!memcmp(game + calls[i].rva + 5, calls[i].bytes + (calls[i].rva - calls[i].base) + 5, 8));
    }
    require(damage_item == (void *)(game + HIT_ITEM_RVA) && damage_level == (void *)(game + LEVEL_RVA) && damage_scale == (void *)(game + SCALE_RVA));
    require(damage_modify == (void *)(game + MODIFY_RVA) && damage_distance == (void *)(game + DISTANCE_RVA) && damage_entity == (void *)(game + ENTITY_RVA));
    puts("PASS: the attack tail, the unaware bonus, the victim level check, both health-stage buff calls and the channel attack call go to their stubs");

    static const unsigned char enter[] = {0x48, 0x83, 0xec, 0x28};
    static const unsigned char leave[] = {0x48, 0x83, 0xc4, 0x28, 0xc3};
    memcpy(call - sizeof(enter), enter, sizeof(enter));
    memcpy(call + 5, leave, sizeof(leave));
    attack_site = tail;
    unaware_site = call - sizeof(enter);
    damage_record = fake_record;
    damage_item = fake_item;
    damage_level = fake_level;
    damage_scale = fake_scale;
    damage_distance = fake_distance;
    damage_modify = fake_modify;
    damage_entity = fake_entity;
    m4_buff_original = (void *)fake_buff;

    unsigned char records[NO_RECORD][0x20] = {{0}};
    memcpy(records[M4_WEAPON] + GUID_OFFSET, M4_GUIDS[0], 16);
    memcpy(records[OTHER_WEAPON] + GUID_OFFSET, M4_GUIDS[0], 16);
    records[OTHER_WEAPON][GUID_OFFSET + 15] ^= 1;
    memcpy(records[M4_ROUND] + GUID_OFFSET, M4_AMMO_GUIDS[0], 16);
    memcpy(records[M4_SPARE_ROUND] + GUID_OFFSET, M4_AMMO_GUIDS[1], 16);
    memcpy(records[HANDGONNE_ROUND] + GUID_OFFSET, M4_AMMO_GUIDS[0], 16);
    records[HANDGONNE_ROUND][GUID_OFFSET] ^= 1;
    unsigned char *items[NO_RECORD + 1][2] = {{0}};
    for (int kind = M4_WEAPON; kind <= NO_RECORD; kind++) items[kind][0] = kind == NO_RECORD ? NULL : records[kind];
    unsigned char victim[8];

    for (size_t i = 0; i < sizeof(damage_cases) / sizeof(damage_cases[0]); i++) {
        m4_damage_fixed = damage_cases[i].fixed;
        m4_attack_hundredths = damage_cases[i].attack;
        unsigned char *weapon = damage_cases[i].weapon ? (unsigned char *)items[damage_cases[i].weapon] : NULL;
        unsigned char *ammo = damage_cases[i].ammo ? (unsigned char *)items[damage_cases[i].ammo] : NULL;
        int m4 = damage_cases[i].m4;
        unsigned seen = lines;
        float attack = attack_bridge(140.5f, 100.25f, weapon, ammo, damage_cases[i].victim ? victim : NULL);
        unsigned logged = lines - seen;
        float fixed = damage_cases[i].attack > 0 ? damage_cases[i].attack / 100.0f : 100.25f;
        if (attack != (m4 ? fixed : 140.5f) || logged != (unsigned)(m4 && damage_cases[i].victim)) {
            fprintf(stderr, "FAIL: %s attack %.3f with %u lines\n", damage_cases[i].name, attack, logged);
            exit(1);
        }
        buff_calls = 0;
        seen = lines;
        float bonus = unaware_bridge(weapon, ammo, 50.5f);
        logged = lines - seen;
        if (bonus != (m4 ? 50.5f : 151.5f) || buff_calls != (unsigned)!m4 || logged != (unsigned)m4) {
            fprintf(stderr, "FAIL: %s unaware %.3f with %u buff calls and %u lines\n", damage_cases[i].name, bonus, buff_calls, logged);
            exit(1);
        }

        unsigned char hit[HIT_SIZE] = {0};
        memcpy(hit + HIT_WEAPON_ID, &weapon, sizeof(weapon));
        memcpy(hit + HIT_AMMO_ID, &ammo, sizeof(ammo));
        level_calls = 0;
        seen = lines;
        int level = level_bridge(hit, VICTIM);
        logged = lines - seen;
        if ((level & 0xff) != !m4 || level_calls != (unsigned)!m4 || logged != (unsigned)m4) {
            fprintf(stderr, "FAIL: %s level %d with %u calls and %u lines\n", damage_cases[i].name, level & 0xff, level_calls, logged);
            exit(1);
        }

        scale_stat = ATTACK_STAT;
        scale_factor = 1.5f;
        scale_calls = 0;
        seen = lines;
        float buffed = attack_buff_bridge(hit, BUFFS, CONTEXT, 242.0f);
        logged = lines - seen;
        if (buffed != (m4 ? 242.0f : 363.0f) || scale_calls != 1 || logged != (unsigned)m4) {
            fprintf(stderr, "FAIL: %s attacker buff %.3f with %u calls and %u lines\n", damage_cases[i].name, buffed, scale_calls, logged);
            exit(1);
        }

        float lane[LANE_BODY + 1] = {0};
        lane[2] = 242.0f;
        lane[LANE_DEFENSE + 2] = 2.0f;
        lane[LANE_BODY] = 1.0f;
        scale_stat = HEALTH_STAT;
        const float factors[] = {0.5f, 1.25f, 0.0f};
        for (size_t f = 0; f < sizeof(factors) / sizeof(factors[0]); f++) {
            scale_factor = factors[f];
            scale_calls = 0;
            seen = lines;
            float loss = health_bridge(hit, lane, BUFFS, 18.0f, 240.0f, SMASH);
            logged = lines - seen;
            float expected = m4 && factors[f] > 0.0f ? 18.0f : 18.0f * factors[f];
            if (loss != expected || scale_calls != 1 || logged != (unsigned)m4) {
                fprintf(stderr, "FAIL: %s victim buff %.2f loss %.3f with %u calls and %u lines\n", damage_cases[i].name, factors[f], loss, scale_calls, logged);
                exit(1);
            }
        }
        seen = lines;
        scale_factor = 0.5f;
        require(health_bridge(hit, lane, BUFFS, 0.5f, -2.0f, 1) == (m4 ? 0.5f : 0.25f) && lines == seen);
    }
    puts("PASS: M4 rounds take the fixed attack, skip the unaware bonus, the victim level cut and both buffs; handgonne balls, other weapons and the tuned-off plugin keep every modifier");

    for (size_t i = 0; i < sizeof(falloff_cases) / sizeof(falloff_cases[0]); i++) {
        m4_armour_near = falloff_cases[i].start;
        m4_armour_far = falloff_cases[i].end;
        m4_armour_floor = falloff_cases[i].rest;
        float factor = m4_armour_factor(falloff_cases[i].range);
        if (factor < falloff_cases[i].factor - 0.0001f || factor > falloff_cases[i].factor + 0.0001f) {
            fprintf(stderr, "FAIL: %s factor %.4f\n", falloff_cases[i].name, factor);
            exit(1);
        }
    }
    m4_armour_near = ARMOUR_NEAR_M;
    m4_armour_far = ARMOUR_FAR_M;
    m4_armour_floor = ARMOUR_FLOOR_PERCENT;
    m4_damage_fixed = 1;
    m4_attack_hundredths = 2200;
    for (size_t i = 0; i < sizeof(armour_cases) / sizeof(armour_cases[0]); i++) {
        unsigned char hit[HIT_SIZE] = {0};
        unsigned char *ammo = (unsigned char *)items[armour_cases[i].m4 ? M4_ROUND : HANDGONNE_ROUND];
        memcpy(hit + HIT_AMMO_ID, &ammo, sizeof(ammo));
        float lane[LANE_BODY + 1] = {0};
        lane[armour_cases[i].attack_slot] = 242.0f;
        lane[LANE_DEFENSE + armour_cases[i].attack_slot] = armour_cases[i].defense;
        fake_range = armour_cases[i].range;
        modify_calls = 0;
        unsigned seen = lines;
        float attack = armour_bridge(hit, 0x3c, 242.0f, lane, armour_cases[i].channel);
        if (attack < armour_cases[i].attack - 0.05f || attack > armour_cases[i].attack + 0.05f || modify_calls != 1 || (modify_flag & 0xff) != 0x3c || lines != seen) {
            fprintf(stderr, "FAIL: %s attack %.2f with %u modify calls\n", armour_cases[i].name, attack, modify_calls);
            exit(1);
        }
    }
    puts("PASS: against armour the M4 attack falls from full at 50 m to 85% at 150 m; bare flesh, unknown channels and other rounds keep the full attack");

    unsigned char m4_hit_record[HIT_SIZE] = {0};
    unsigned char *m4_ammo = (unsigned char *)items[M4_ROUND];
    memcpy(m4_hit_record + HIT_AMMO_ID, &m4_ammo, sizeof(m4_ammo));
    float head[LANE_BODY + 1] = {0};
    head[2] = 242.0f;
    head[LANE_BODY] = HEAD;
    scale_factor = 0.5f;
    fake_range = 12.0f;
    require(health_bridge(m4_hit_record, head, BUFFS, 18.0f, 242.0f, SMASH) == 18.0f);
    require(strstr(last_line, "M4 hit head x1.50 smash at 12.0 m attack 242.0 x1.00 = 242.0 defense 0.0 dmgr 242.0 health loss 126.0 of 100, victim buff 0.500 skipped"));
    head[LANE_BODY] = 1.0f;
    head[LANE_DEFENSE + 2] = 35.0f;
    fake_range = 90.0f;
    require(health_bridge(m4_hit_record, head, BUFFS, 40.0f, 206.5f, SMASH) == 40.0f);
    require(strstr(last_line, "M4 hit torso x1.00 smash at 90.0 m attack 242.0 x0.94 = 227.5 defense 35.0 dmgr 206.5") && strstr(last_line, "health loss 200.0 of 100"));
    head[LANE_DEFENSE + 2] = 0.0f;
    scale_factor = 0.0f;
    require(health_bridge(m4_hit_record, head, BUFFS, 12.0f, 242.0f, SMASH) == 0.0f);
    require(strstr(last_line, "victim buff 0.000 kept, the victim cannot be hurt"));
    puts("PASS: the hit line names the body part, channel, distance, attack, armour factor, scaled attack, defense, dmgr and health loss clamped at 200");

    scale_factor = 0.5f;
    head[LANE_BODY] = HEAD;
    float *where = (float *)(m4_hit_record + HIT_POSITION);
    where[0] = 12.25f;
    where[1] = -3.5f;
    where[2] = 100.125f;
    unsigned seen = lines;
    require(health_bridge(m4_hit_record, head, BUFFS, 18.0f, 242.0f, SMASH) == 18.0f && lines == seen + 2);
    require(set_calls == 0 && head_seq == 0);
    require(m4_main_thread == 0 && first_thread() == GetCurrentThreadId());

    void **console_table = (void **)(game + CONSOLE_VTABLE_RVA);
    void **cvar_table = (void **)(game + STRING_CVAR_VTABLE_RVA);
    console_table[CONSOLE_REGISTER_STRING / 8] = (void *)fake_register_string;
    console_table[CONSOLE_GET_CVAR / 8] = (void *)fake_get_cvar;
    cvar_table[CVAR_SET_STRING / 8] = (void *)fake_set_string;
    cvar_table[CVAR_CLEAR_FLAGS / 8] = (void *)fake_clear_flags;
    fake_missing = "ca_UseLookIK";
    void *console_object = console_table, *other_object = cvar_table;
    fake_cvar_object = cvar_table;
    fake_qam_object = cvar_table;
    void **console_global = (void **)(game + CONSOLE_GLOBAL_RVA);
    *console_global = NULL;
    require(!head_watch(game, 0) && strstr(last_line, "never appeared"));
    *console_global = &other_object;
    require(!head_watch(game, 0) && strstr(last_line, "never appeared"));
    require(console_table[CONSOLE_GET_CVAR / 8] == (void *)fake_get_cvar && m4_main_thread == 0);
    *console_global = &console_object;
    require(head_watch(game, 0) && m4_main_thread == GetCurrentThreadId());
    require(console_table[CONSOLE_GET_CVAR / 8] == (void *)m4_console_get && console_get == fake_get_cvar);
    require(register_calls == 0 && !head_cvar);

    HANDLE early = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(void *)call_get_cvar, &console_object, 0, NULL);
    require(early && WaitForSingleObject(early, 5000) == WAIT_OBJECT_0);
    CloseHandle(early);
    require(register_calls == 0 && !head_cvar && get_calls == 1 && clear_calls == 0);
    require(call_get_cvar(&console_object, "sys_spec") == &fake_cvar_object);
    require(register_calls == 1 && head_cvar == &fake_cvar_object && get_calls == 5 && clear_calls == 2);
    require(lua_cvars_open == (LUA_CVARS_ALL & ~2u));
    fake_missing = NULL;
    require(!strcmp(cvar_text, HEAD_CVAR_START) && strstr(last_line, "m4_head_hit created as \"0 0 0 0\""));
    require(call_get_cvar(&console_object, "sys_spec") == &fake_cvar_object && register_calls == 1 && get_calls == 7 && set_calls == 0);
    require(clear_calls == 3 && lua_cvars_open == LUA_CVARS_ALL && strstr(last_line, "ca_UseLookIK no longer needs -devmode"));
    require(call_get_cvar(&console_object, "sys_spec") == &fake_cvar_object && get_calls == 8 && clear_calls == 3);
    puts("PASS: the cvars the mod's Lua sets lose their cheat lock on the main thread's console reads, a missing one is retried");
    static const struct { const char *name; unsigned taps; } escapes[] = {{"sys_spec", 0}, {"m4_ui_cancel", 1}, {"m4_ui_cancel_x", 0}, {"m4_ui_cancel", 1}};
    for (size_t e = 0; e < sizeof(escapes) / sizeof(escapes[0]); e++) {
        unsigned before = escape_taps;
        call_get_cvar(&console_object, escapes[e].name);
        require(escape_taps == before + escapes[e].taps);
    }
    get_calls -= sizeof(escapes) / sizeof(escapes[0]);
    puts("PASS: a main-thread read of m4_ui_cancel taps Esc once, other names never do");
    static const struct { const char *name; LONG dot; } sight_lines[] = {{"m4_sight_dot", 1}, {"sys_spec", 1}, {"m4_sight_irons", 0}, {"m4_sight_dotx", 0}, {"m4_sight_dot", 1}};
    for (size_t l = 0; l < sizeof(sight_lines) / sizeof(sight_lines[0]); l++) {
        call_get_cvar(&console_object, sight_lines[l].name);
        require(m4_sight_dot == sight_lines[l].dot);
    }
    get_calls -= sizeof(sight_lines) / sizeof(sight_lines[0]);
    puts("PASS: m4_sight_dot puts the sight solve on the red dot's line and m4_sight_irons puts it back on the irons");
    static const struct { const char *name; LONG picking; } picks[] = {{"m4_sight_pickx", 0}, {"m4_sight_dot", 0}, {"m4_sight_pick", 1}};
    for (size_t k = 0; k < sizeof(picks) / sizeof(picks[0]); k++) {
        call_get_cvar(&console_object, picks[k].name);
        require(m4_sight_picking == picks[k].picking);
    }
    get_calls -= sizeof(picks) / sizeof(picks[0]);
    puts("PASS: a read of m4_sight_pick arms the picker's M4 swallow, other names never do");

    require(health_bridge(m4_hit_record, head, BUFFS, 18.0f, 242.0f, SMASH) == 18.0f);
    require(set_calls == 1 && !strcmp(cvar_text, "1 12.250 -3.500 100.125"));
    head[LANE_BODY] = 1.0f;
    require(health_bridge(m4_hit_record, head, BUFFS, 18.0f, 242.0f, SMASH) == 18.0f && set_calls == 1);
    head[LANE_BODY] = 0.7f;
    require(health_bridge(m4_hit_record, head, BUFFS, 18.0f, 242.0f, SMASH) == 18.0f && set_calls == 1);
    unsigned char ball_record[HIT_SIZE];
    memcpy(ball_record, m4_hit_record, sizeof(ball_record));
    unsigned char *ball = (unsigned char *)items[HANDGONNE_ROUND];
    memcpy(ball_record + HIT_AMMO_ID, &ball, sizeof(ball));
    head[LANE_BODY] = HEAD;
    require(health_bridge(ball_record, head, BUFFS, 18.0f, 242.0f, SMASH) == 9.0f && set_calls == 1);
    fake_entity_table[ENTITY_WORLD_POS / 8] = (void *)fake_world_pos;
    void *victim_actor = VICTIM;
    memcpy(m4_hit_record + HIT_VICTIM, &victim_actor, sizeof(victim_actor));
    memset(where, 0, 3 * sizeof(float));
    seen = lines;
    require(health_bridge(m4_hit_record, head, BUFFS, 18.0f, 242.0f, SMASH) == 18.0f && lines == seen + 2);
    require(set_calls == 2 && !strcmp(cvar_text, "2 7.500 8.250 30.000"));
    require(strstr(last_line, "health loss") && head_seq == 2);
    where[0] = 1.0f;
    head_hit = m4_hit_record;
    head_lane = head;
    HANDLE late = CreateThread(NULL, 0, off_thread_hit, NULL, 0, NULL);
    require(late && WaitForSingleObject(late, 5000) == WAIT_OBJECT_0);
    CloseHandle(late);
    require(set_calls == 2 && head_waiting && !strcmp(cvar_text, "2 7.500 8.250 30.000"));
    require(call_get_cvar(&console_object, HEAD_CVAR) == &fake_cvar_object);
    require(set_calls == 3 && !head_waiting && !strcmp(cvar_text, "3 1.000 0.000 0.000"));
    require(call_get_cvar(&console_object, HEAD_CVAR) == &fake_cvar_object && set_calls == 3);
    puts("PASS: m4_head_hit is created as \"0 0 0 0\" on the main thread's first console read and every M4 head hit sets \"<seq> x y z\", off-thread hits on the next read");

    m4_damage_fixed = 1;
    damage_lines = DAMAGE_LINES;
    seen = lines;
    require(attack_bridge(140.5f, 100.25f, NULL, (unsigned char *)items[M4_ROUND], victim) == 22.0f);
    require(unaware_bridge(NULL, (unsigned char *)items[M4_ROUND], 50.5f) == 50.5f);
    require((level_bridge(m4_hit_record, VICTIM) & 0xff) == 0);
    scale_factor = 0.5f;
    require(health_bridge(m4_hit_record, head, BUFFS, 18.0f, 242.0f, SMASH) == 18.0f);
    require(lines == seen && set_calls == 4 && !strcmp(cvar_text, "4 1.000 0.000 0.000"));
    puts("PASS: past the line cap M4 hits stay fixed, head hits still publish, and nothing logs");

    static const struct { int states[8]; const char *text; } bars[] = {
        {{3, 2, 0, 0, 0, 0, 0, 0}, "1 0"},
        {{3, 3, 0, 0, 0, 0, 0, 0}, "1 0"},
        {{3, 1, 0, 0, 0, 0, 0, 0}, "0 0"},
        {{2, 2, 3, 1, 2, 2, 1, 1}, "5 1"},
        {{2, 1, 2, 2, 1, 1, 0, 0}, "2 -1"},
    };
    for (unsigned b = 0; b < sizeof(bars) / sizeof(bars[0]); b++) {
        unsigned char slots[8][0x20] = {{0}};
        unsigned char *values[16] = {0};
        for (int i = 0; i < 8; i++) {
            memcpy(slots[i] + QAM_VALUE_STATE, &bars[b].states[i], sizeof(int));
            values[i * 2] = slots[i];
        }
        unsigned char *array[2] = {(unsigned char *)values, (unsigned char *)(values + 16)};
        char text[32];
        require(m4_qam_rows((unsigned char *)array, text, sizeof(text)) && !strcmp(text, bars[b].text));
    }
    unsigned char *short_values[4] = {0};
    unsigned char *short_array[2] = {(unsigned char *)short_values, (unsigned char *)(short_values + 4)};
    char short_text[32];
    require(!m4_qam_rows((unsigned char *)short_array, short_text, sizeof(short_text)));
    puts("PASS: the weapon bar's eight slots give the rows whose off-hand slot holds ammo and the row in hand; a bar of another size is ignored");
    VirtualFree(game, 0, MEM_RELEASE);
    return 0;
}
