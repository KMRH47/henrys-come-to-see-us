#include "m4_ammo.c"
#include <stdlib.h>
#include "m4_cadence.h"
#define install install_sound
#include "m4_sound.c"
#undef install

static unsigned confirmed_shots;
int m4_shot_fired(void) { confirmed_shots++; return 0; }
void m4_in_hand(void) {}
static const char *one_shot;
void m4_clock_one_shot(const char *name, long volume_percent) { one_shot = name; }

#define require(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

void note(const char *text) {
    puts(text);
}

void m4_frame(void) {}
void m4_tag_disarm(void) {}
void m4_tag_draw(int m4) { (void)m4; }
void m4_tag_drawn(int m4) { (void)m4; }

#define COUNT(list) (sizeof(list) / sizeof((list)[0]))

typedef struct {
    double from, to, shot;
    int physical;
    long clicks;
    int enabled, aim;
} Span;

// Steps the cadence one millisecond at a time through each span and compares
// every press (D) and release (U) it sends with the expected timeline.
static void expect_presses(Cadence *cadence, const Span *spans, size_t count, const char *expected) {
    char seen[1024] = "";
    int used = 0;
    for (size_t i = 0; i < count; i++)
        for (double now = spans[i].from; now < spans[i].to; now += 1) {
            CadenceInput input = {.now = now, .shot_at = spans[i].shot, .clicks = spans[i].clicks,
                                  .physical = spans[i].physical, .enabled = spans[i].enabled, .aim = spans[i].aim};
            int action = cadence_step(cadence, &input, 100, 45);
            if (action != CADENCE_NONE)
                used += snprintf(seen + used, sizeof(seen) - used, "%s%c%.0f", used ? " " : "",
                                 action == CADENCE_DOWN ? 'D' : 'U', now);
        }
    if (strcmp(seen, expected)) {
        fprintf(stderr, "FAIL presses\n  want %s\n  got  %s\n", expected, seen);
        exit(1);
    }
}

// A game that reads the button at the start of each frame, fires 2 ms into the
// second frame after it sees a fresh press unless that shot would come under 84 ms
// after the last, and plays the frame mark 5 ms in. The shots it fires must all be the same number of frames
// apart, and that number must be the one closest to the 110 ms period.
static void expect_even_shots(double frame_ms) {
    Cadence cadence = {0};
    double shots[40], shot_at = 0, last_shot = -1000;
    int count = 0, sent = 0, polled = 0;
    long frame = -1, ticks = 0, shot_frame = 0, fire_frame = -1;
    for (double now = 0; now < 8000 && count < 40; now += 0.5) {
        long current = (long)(now / frame_ms);
        double into = now - current * frame_ms;
        if (current != frame) {
            frame = current;
            if (sent && !polled && now + 2 * frame_ms - last_shot >= 84) fire_frame = frame + 2;
            polled = sent;
        }
        if (into >= 5 && into < 5.5) ticks++;
        if (frame == fire_frame && into >= 2 && into < 2.5) {
            last_shot = shot_at = shots[count++] = now;
            shot_frame = ticks;
            fire_frame = -1;
        }
        CadenceInput input = {.now = now, .shot_at = shot_at, .frame_ms = frame_ms, .frame = ticks,
                              .shot_frame = shot_frame, .clicks = 1, .physical = 1, .enabled = 1, .aim = AIM_READY};
        int action = cadence_step(&cadence, &input, 110, 45);
        if (action != CADENCE_NONE) sent = action == CADENCE_DOWN;
    }
    double gap = shots[5] - shots[4];
    if (count != 40 || gap > 110 + 2 * frame_ms) {
        fprintf(stderr, "FAIL at %.2f ms frames: %d shots, %.1f ms apart\n", frame_ms, count, gap);
        exit(1);
    }
    for (int i = 5; i < count; i++)
        if (fabs(shots[i] - shots[i - 1] - gap) > 1) {
            fprintf(stderr, "FAIL at %.2f ms frames: shot %d came %.1f ms after the last, not %.1f\n", frame_ms, i,
                    shots[i] - shots[i - 1], gap);
            exit(1);
        }
}

static void expect_ready_after_shot(void) {
    static const struct { double frame_ms, ready_ms; } cases[] = {{19.5, 60}, {19.5, 75}, {16.67, 60}, {12, 70}, {22, 65}};
    for (size_t i = 0; i < COUNT(cases); i++) {
        double frame_ms = cases[i].frame_ms, ready_ms = cases[i].ready_ms;
        Cadence cadence = {0};
        double shots[40], shot_at = 0, last_shot = -1000, charged_at = 0;
        int count = 0, down = 0, fresh = 0, refused = 0;
        long shot_frame = 0, fire_frame = -1;
        for (long frame = 0; frame < 2000 && count < 40; frame++) {
            double now = frame * frame_ms;
            if (frame == fire_frame) last_shot = shot_at = shots[count++] = now, shot_frame = frame, fire_frame = -1;
            if (count && charged_at < last_shot && now - last_shot >= ready_ms) charged_at = now;
            if (fresh && fire_frame < 0) {
                if (charged_at > last_shot) fire_frame = frame + 2;
                else refused++;
            }
            fresh = 0;
            CadenceInput input = {.now = now, .shot_at = shot_at, .frame_ms = frame_ms, .charged_at = charged_at, .frame = frame,
                                  .shot_frame = shot_frame, .clicks = 1, .physical = 1, .enabled = 1, .aim = AIM_READY};
            int action = cadence_step(&cadence, &input, 70, 45);
            if (action == CADENCE_DOWN && !down) fresh = 1;
            if (action != CADENCE_NONE) down = action == CADENCE_DOWN;
        }
        double floor = (ceil(ready_ms / frame_ms) + 2) * frame_ms, slowest = 0, quickest = 1e9;
        for (int k = 3; k < count; k++) slowest = fmax(slowest, shots[k] - shots[k - 1]), quickest = fmin(quickest, shots[k] - shots[k - 1]);
        if (count != 40 || refused > 1 || slowest > floor + frame_ms + 0.5 || slowest - quickest > 0.5) {
            fprintf(stderr, "FAIL charged %.0f ms at %.2f ms frames: %d shots, %d refused, %.1f-%.1f ms apart, floor %.1f\n",
                    ready_ms, frame_ms, count, refused, quickest, slowest, floor);
            exit(1);
        }
    }
    puts("PASS: each press waits for the gun to report charged, so every shot lands the same gap apart and none is refused");
}

// The engine refuses presses until it is charged at frame 20, then fires 2
// frames after a fresh press. The first shot must land within one retry of that.
static void expect_first_shot(void) {
    Cadence cadence = {0};
    double shot_at = 0;
    long shot_frame = 0, fire_frame = -1;
    for (long frame = 0; frame < 60 && !shot_at; frame++) {
        if (frame == fire_frame) shot_at = frame * 12.0, shot_frame = frame;
        CadenceInput input = {.now = frame * 12.0, .shot_at = shot_at, .frame_ms = 12, .frame = frame,
                              .shot_frame = shot_frame, .clicks = 1, .physical = 1, .enabled = 1, .aim = AIM_READY};
        int action = cadence_step(&cadence, &input, 110, 45);
        if (action == CADENCE_DOWN && frame >= 20) fire_frame = frame + 2;
    }
    if (!shot_at || shot_frame > 20 + 9 + 2) {
        fprintf(stderr, "FAIL first shot at frame %ld, engine ready at 20\n", shot_frame);
        exit(1);
    }
    puts("PASS: a first press the engine refuses is retried within 9 frames, not 250 ms");
}

// A button the game already holds down, from a click it took during the
// raise, is let go first, so the first real press is a fresh one.
static void expect_stuck_button_released(void) {
    struct { const char *name; long frames, ready_frame, click_frame, down_frame; const char *want; } cases[] = {
        {"a button the game reports down", 4, 0, 0, 0, "U0 D2"},
        {"a click during the raise the game took unseen", 22, 20, 2, -1, "U2 D20"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Cadence cadence = {0};
        char seen[64] = "";
        int used = 0;
        for (long frame = 0; frame < cases[i].frames; frame++) {
            CadenceInput input = {.now = frame * 12.0, .frame_ms = 12, .frame = frame,
                                  .clicks = frame >= cases[i].click_frame, .physical = frame >= cases[i].click_frame,
                                  .enabled = 1, .aim = frame >= cases[i].ready_frame ? AIM_READY : AIM_RAISING,
                                  .game_down = frame == cases[i].down_frame};
            int action = cadence_step(&cadence, &input, 110, 45);
            if (action != CADENCE_NONE)
                used += snprintf(seen + used, sizeof(seen) - used, "%s%c%ld", used ? " " : "", action == CADENCE_DOWN ? 'D' : 'U', frame);
        }
        if (strcmp(seen, cases[i].want)) {
            fprintf(stderr, "FAIL %s\n  want %s\n  got  %s\n", cases[i].name, cases[i].want, seen);
            exit(1);
        }
        printf("PASS: %s is let go before the first press\n", cases[i].name);
    }
}

static int raw_type(unsigned char *record) {
    return *(int *)(record + 0xf0);
}

static const unsigned char SHOT_BALL_GUID[16] = {0x12, 0xed, 0x0d, 0xf1, 0x1c, 0xa4, 0xbf, 0x40, 0xa8, 0xae, 0x88, 0x3d, 0x4e, 0x84, 0x50, 0x59};

int main(void) {
    unsigned char *game = VirtualAlloc(NULL, 0x3000000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    require(game);
    memcpy(game + LOOKUP_RVA, LOOKUP_BYTES, sizeof(LOOKUP_BYTES));
    memcpy(game + LOOKUP_TAIL_RVA, TAIL_BYTES, sizeof(TAIL_BYTES));
    memcpy(game + RECORD_CLASS_RVA, RECORD_CLASS_BYTES, sizeof(RECORD_CLASS_BYTES));
    memcpy(game + AMMO_TYPE_RVA, AMMO_TYPE_BYTES, sizeof(AMMO_TYPE_BYTES));
    memcpy(game + EQUIP_MATCH_RVA, EQUIP_MATCH_BYTES, sizeof(EQUIP_MATCH_BYTES));
    const unsigned char inline_entry[] = {0x53, 0x55, 0x41, 0x55, 0x48, 0x81, 0xec, 0x00, 0x01, 0x00, 0x00,
        0x48, 0x8d, 0xac, 0x24, 0x80, 0x00, 0x00, 0x00, 0x49, 0x89, 0xe5, 0x31, 0xdb, 0x48, 0x89, 0xc8};
    const unsigned char inline_exit[] = {0x0f, 0x4d, 0xc3, 0x48, 0x81, 0xc4, 0x00, 0x01, 0x00, 0x00, 0x41, 0x5d, 0x5d, 0x5b, 0xc3};
    int (*masked_type[MASK_CHECKS])(unsigned char *);
    for (size_t i = 0; i < MASK_CHECKS; i++) {
        unsigned char *site = game + mask_checks[i].rva;
        memcpy(site - sizeof(inline_entry), inline_entry, sizeof(inline_entry));
        memcpy(site, mask_checks[i].bytes, mask_checks[i].length);
        memcpy(site + mask_checks[i].length, inline_exit, sizeof(inline_exit));
        masked_type[i] = (void *)(site - sizeof(inline_entry));
    }
    unsigned char (*compatible)(void *, int *) = (void *)(game + EQUIP_MATCH_RVA);
    void *vtable[11] = {0};
    vtable[10] = (void *)raw_type;
    unsigned char record[0x100] = {0};
    *(void **)record = vtable;
    memcpy(record + GUID_OFFSET, M4_AMMO_GUIDS[0], 16);
    *(int *)(record + 0xf0) = 3;
    unsigned char cls[0x100] = {0};
    cls[MASK_OFFSET] = M4_MASK;
    cls[AUTO_LOAD_OFFSET] = 0;
    cls[REQUIRES_COCKING_OFFSET] = 1;
    cls[LOADING_MOVEMENT_OFFSET] = 2;
    cls[MOVEMENT_CROUCH_OFFSET] = 0;
    void *head[1], *node[3] = {head, NULL, cls}, *equipment[2] = {NULL, head};
    head[0] = node;
    for (size_t i = 0; i < MASK_CHECKS; i++) {
        int selected_type = masked_type[i](record);
        require(selected_type == 3 && !compatible(equipment, &selected_type));
    }
    puts("The six inline mask checks read the round's raw ball type, which the M4's mask refuses, so the patch must return the private type");
    size_t call_count = sizeof(loading_calls) / sizeof(loading_calls[0]);
    for (size_t i = 0; i < call_count; i++)
        memcpy(game + loading_calls[i].rva, loading_calls[i].bytes, loading_calls[i].length);
    for (size_t bad = 0; bad < call_count; bad++) {
        game[loading_calls[bad].rva + 7] ^= 1;
        install(game);
        require(game[LOOKUP_RVA] == LOOKUP_BYTES[0]);
        require(game[LOOKUP_TAIL_RVA] == TAIL_BYTES[0]);
        for (size_t i = 0; i < call_count; i++)
            require(!memcmp(game + loading_calls[i].rva, loading_calls[i].bytes, 5));
        game[loading_calls[bad].rva + 7] ^= 1;
    }
    size_t equipment_sites[MASK_CHECKS + 1] = {EQUIP_MATCH_RVA};
    for (size_t i = 0; i < MASK_CHECKS; i++) equipment_sites[i + 1] = mask_checks[i].rva + mask_checks[i].length - 1;
    for (size_t i = 0; i < sizeof(equipment_sites) / sizeof(equipment_sites[0]); i++) {
        game[equipment_sites[i]] ^= 1;
        install(game);
        require(game[LOOKUP_RVA] == LOOKUP_BYTES[0]);
        require(game[LOOKUP_TAIL_RVA] == TAIL_BYTES[0]);
        for (size_t j = 0; j < call_count; j++)
            require(!memcmp(game + loading_calls[j].rva, loading_calls[j].bytes, 5));
        game[equipment_sites[i]] ^= 1;
    }
    install(game);
    for (size_t i = 0; i < MASK_CHECKS; i++) {
        int selected_type = masked_type[i](record);
        require(selected_type == 0 && compatible(equipment, &selected_type));
    }
    require(raw_type(record) == 3 && engine_ammo_type(record) == 3);
    require(game[LOOKUP_RVA] == 0xe9);
    require(game[LOOKUP_TAIL_RVA] == 0xe9);
    require(!memcmp(game + AMMO_TYPE_RVA, AMMO_TYPE_BYTES, sizeof(AMMO_TYPE_BYTES)));
    for (int item = 0; item < 3; item++) {
        memset(record + GUID_OFFSET, 0, 16);
        if (item < 2) memcpy(record + GUID_OFFSET, M4_AMMO_GUIDS[item], 16);
        for (int type = 0; type < 7; type++) {
            *(int *)(record + 0xf0) = type;
            int vanilla = type < 5 ? type : 0;
            int expected = item < 2 ? 0 : vanilla;
            require(engine_ammo_type(record) == vanilla);
            for (size_t site = 0; site < MASK_CHECKS; site++) {
                int equipment_type = masked_type[site](record);
                require(equipment_type == expected);
                cls[MASK_OFFSET] = M4_MASK;
                require(compatible(equipment, &equipment_type) == !!(M4_MASK & (1 << expected)));
                cls[MASK_OFFSET] = 0x18;
                require(compatible(equipment, &equipment_type) == !!(0x18 & (1 << expected)));
            }
            for (size_t i = 0; i < call_count; i++) {
                unsigned char *call = game + loading_calls[i].rva;
                require(call[0] == 0xe8);
                int (*loading)(unsigned char *) = (void *)(call + 5 + *(INT32 *)(call + 1));
                require(loading(record) == expected);
            }
        }
    }
    cls[MASK_OFFSET] = 0x18;
    memcpy(record + GUID_OFFSET, M4_GUID, 16);
    require(on_weapon_class(record, cls, NULL) == cls && cls[MASK_OFFSET] == M4_MASK);
    memset(record + GUID_OFFSET, 0, 16);
    require(on_weapon_class(record, cls, NULL) == cls && cls[MASK_OFFSET] == 0x18);
    memcpy(record + GUID_OFFSET, M4_GUID, 16);
    require(on_weapon_class(record, cls, NULL) == cls);
    m4_class_flags(1, 0);
    require(cls[AUTO_LOAD_OFFSET] == 1 && cls[REQUIRES_COCKING_OFFSET] == 0 && cls[LOADING_MOVEMENT_OFFSET] == 2 && cls[MOVEMENT_CROUCH_OFFSET] == 1);
    m4_class_flags(1, 1);
    require(cls[AUTO_LOAD_OFFSET] == 1 && cls[REQUIRES_COCKING_OFFSET] == 0 && cls[LOADING_MOVEMENT_OFFSET] == 0 && cls[MOVEMENT_CROUCH_OFFSET] == 1);
    m4_class_flags(0, 0);
    require(cls[AUTO_LOAD_OFFSET] == 0 && cls[REQUIRES_COCKING_OFFSET] == 1 && cls[LOADING_MOVEMENT_OFFSET] == 2 && cls[MOVEMENT_CROUCH_OFFSET] == 0);
    cls[LOADING_MOVEMENT_OFFSET] = 5;
    m4_class_flags(1, 0);
    require(cls[LOADING_MOVEMENT_OFFSET] == 5);
    m4_class_flags(0, 0);
    puts("PASS: the M4 holds its own load flags while it is in hand and the class row is handed back on release");
    memcpy(record + GUID_OFFSET, SHOT_BALL_GUID, 16);
    *(int *)(record + 0xf0) = 3;
    cls[MASK_OFFSET] = M4_MASK;
    for (size_t i = 0; i < call_count; i++) {
        unsigned char *call = game + loading_calls[i].rva;
        int (*loading)(unsigned char *) = (void *)(call + 5 + *(INT32 *)(call + 1));
        int type = loading(record);
        require(type == 3 && !compatible(equipment, &type));
    }
    for (size_t i = 0; i < MASK_CHECKS; i++) {
        int type = masked_type[i](record);
        require(type == 3 && !compatible(equipment, &type));
    }
    cls[MASK_OFFSET] = 0x18;
    for (size_t i = 0; i < call_count; i++) {
        unsigned char *call = game + loading_calls[i].rva;
        int (*loading)(unsigned char *) = (void *)(call + 5 + *(INT32 *)(call + 1));
        int type = loading(record);
        require(type == 3 && compatible(equipment, &type));
    }
    puts("PASS: a ball ammo item is refused at every loading site and every mask check while the M4's mask is up, and loads under the handgonnes' table mask");
    Cadence cadence = {0};
    static const Span held[] = {
        {1000, 1010, 0, 1, 1, 1, AIM_READY}, {1010, 1180, 1010, 1, 1, 1, AIM_READY},
        {1180, 1530, 1180, 1, 1, 1, AIM_READY}, {1530, 1590, 1530, 1, 1, 1, AIM_READY},
        {1590, 1600, 1530, 0, 1, 1, AIM_READY},
    };
    expect_presses(&cadence, held, COUNT(held), "D1000 U1045 D1100 U1180 D1225 U1475 D1520 U1565");
    require(!cadence.active);
    puts("PASS: presses keep a fixed clock, the button stays down until the shot lands, a press that never fires is let go after 250 ms and retried, and a late release keeps the clock");
    static const Span clicked[] = {
        {1600, 1620, 1290, 1, 2, 1, AIM_RAISING}, {1620, 2200, 1290, 0, 2, 1, AIM_RAISING},
        {2200, 2230, 1290, 0, 2, 1, AIM_READY}, {2230, 2240, 2230, 0, 2, 1, AIM_READY},
        {3000, 3030, 2230, 1, 3, 1, AIM_READY}, {3030, 3300, 2230, 0, 3, 1, AIM_READY},
        {3300, 3310, 3300, 0, 3, 1, AIM_READY},
        {4000, 4010, 3300, 1, 4, 1, AIM_READY}, {4010, 5510, 3300, 0, 4, 1, AIM_READY},
    };
    expect_presses(&cadence, clicked, COUNT(clicked),
                   "U1600 D2200 U2231 D3000 U3250 D3295 U3301 D4000 U4250 D4295 U4545 D4590 U4840 D4885 U5135 D5180 U5430 D5475 U5501");
    require(!cadence.active && !cadence.latched);
    puts("PASS: a click during the raise waits and fires once the sight is ready; a tap the engine drops retries until it fires; a click that never fires expires");
    static const Span unaimed[] = {
        {6000, 6020, 3300, 1, 5, 1, AIM_NONE}, {6020, 6030, 3300, 0, 5, 1, AIM_NONE},
        {7000, 7010, 3300, 1, 6, 1, AIM_READY}, {7010, 7020, 3300, 1, 6, 1, AIM_RAISING},
        {8000, 8110, 3300, 1, 7, 1, AIM_NONE}, {8110, 10000, 3300, 1, 7, 0, AIM_NONE},
        {10000, 10001, 3300, 1, 7, 1, AIM_NONE}, {10001, 10100, 3300, 0, 7, 0, AIM_NONE},
        {10100, 10200, 3300, 0, 8, 0, AIM_READY}, {10200, 10210, 3300, 0, 8, 1, AIM_READY},
    };
    expect_presses(&cadence, unaimed, COUNT(unaimed), "D6000 U6020 D7000 U7010 D8000 D10000 U10001");
    require(!cadence.active);
    puts("PASS: unaimed clicks pass straight through; a new raise lifts the button; disarm hands the button back and clicks made while disarmed never fire later");
    expect_first_shot();
    expect_ready_after_shot();
    expect_stuck_button_released();
    static const double frame_times[] = {8.33, 11.1, 16.67, 20, 25, 31.25, 33.33};
    for (size_t i = 0; i < COUNT(frame_times); i++) expect_even_shots(frame_times[i]);
    puts("PASS: with the frame mark ticking, every shot lands the same number of frames after the last from 30 to 120 fps");
    void *player = record, *npc = cls;
    require(on_trigger(player, SHOT_TRIGGER) == SHOT_TRIGGER && confirmed_shots == 0);
    require(on_trigger(player, IN_HAND_TRIGGER) == IN_HAND_TRIGGER);
    require(on_trigger(npc, SHOT_TRIGGER) == SHOT_TRIGGER && confirmed_shots == 0);
    require(on_trigger(player, SHOT_TRIGGER) == M4_SHOT_TRIGGER && confirmed_shots == 1);
    require(on_trigger(player, 123) == 123 && confirmed_shots == 1);
    for (size_t i = 0; i < sizeof(RELOAD_TRIGGERS) / sizeof(RELOAD_TRIGGERS[0]); i++) {
        require(on_trigger(player, RELOAD_TRIGGERS[i]) == SILENT_TRIGGER);
        require(on_trigger(npc, RELOAD_TRIGGERS[i]) == RELOAD_TRIGGERS[i]);
    }
    require(on_trigger(player, OUT_OF_HAND_TRIGGER) == OUT_OF_HAND_TRIGGER);
    for (size_t i = 0; i < sizeof(ONE_SHOTS) / sizeof(ONE_SHOTS[0]); i++) {
        one_shot = NULL;
        require(on_trigger(npc, ONE_SHOTS[i].trigger) == ONE_SHOTS[i].trigger && one_shot == ONE_SHOTS[i].wav);
    }
    require(on_trigger(player, SHOT_TRIGGER) == SHOT_TRIGGER && confirmed_shots == 1);
    require(on_trigger(player, RELOAD_TRIGGERS[0]) == RELOAD_TRIGGERS[0]);
    require(on_trigger(player, IN_HAND_TRIGGER) == IN_HAND_TRIGGER);
    require(on_trigger(player, SHOT_TRIGGER) == M4_SHOT_TRIGGER && confirmed_shots == 2);
    puts("PASS: only a marked player's shot advances cadence; the M4 in hand silences the seven handgonne reload sounds; NPCs, other triggers and unequipped weapons keep theirs; reload marks play their WAV on any proxy");
    VirtualFree(game, 0, MEM_RELEASE);
    return 0;
}
