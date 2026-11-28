#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mmsystem.h>
#include "m4_cadence.h"
#include "m4_hip.h"
#include "m4_recoil.h"


#define LOG_POLL_MS 10
#define ARMED_PREFIX "M4CARBINE auto armed "
#define AIM_PREFIX "M4CARBINE auto aim "
#define RECHARGE_PREFIX "M4CARBINE auto recharge "
#define MAG_PREFIX "M4CARBINE auto mag "
#define LOOK_PREFIX "M4CARBINE auto look "
#define RECOIL_PREFIX "M4CARBINE auto recoil "
#define SINGLE_PREFIX "M4CARBINE auto single "
#define DAMAGE_PREFIX "M4CARBINE auto damage "

#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION_FLAG 0x00000002

static volatile LONG REPEAT_PERIOD_MS = 60;
static volatile LONG RELEASE_GAP_MS = 8;
static volatile LONG RECOIL_CAMERA_PERCENT = 250;
static volatile LONG RECOIL_SHAKE_PERCENT = 15;
static volatile LONG RECOIL_COUNTS_PER_DEGREE_HUNDREDTHS = 2273;
static volatile LONG RECOIL_CYCLE_MS = 73;
static volatile LONG RECOIL_SETTLE_PER_S = 1100;
static volatile LONG RECOIL_CEILING_TENTHS = 3600;
static volatile LONG RECOIL_BULLET_PERCENT = 250;
static volatile LONG RECOIL_CONE_PERCENT = 30;
static volatile LONG SHOT_VOLUME_PERCENT = 50;
static volatile LONG SELECTOR_KEY = 'H';

static volatile LONG physical_down = 0;
static volatile LONG physical_aim = 0;
static volatile LONG hip_takeover = 0;
static HipHold hip;
static volatile LONG armed = 0;
static volatile LONG aim = AIM_NONE;
static volatile LONG recharge = -1;
static volatile LONG mag_left = -1;
static volatile LONG recoil_on = 1;
static volatile LONG single_shot = 0;
extern volatile LONG m4_selector_presses;
volatile LONG m4_look_off;
static volatile LONG clicks = 0;
static volatile double aim_pressed_at = 0;
static volatile double click_at = 0;
static volatile double aim_since = 0;
static volatile LONG64 shot_tick;
extern volatile LONG64 charged_tick;
static volatile LONG64 frame_tick;
static volatile LONG frames;
static volatile LONG shot_frame;
static volatile double frame_time;
static HANDLE engage;
static LARGE_INTEGER frequency;
char game_root[MAX_PATH];
DWORD WINAPI ammo_hook(LPVOID unused);
DWORD WINAPI sound_hook(LPVOID unused);
DWORD WINAPI impact_hook(LPVOID unused);
DWORD WINAPI mass_hook(LPVOID unused);
DWORD WINAPI hip_hook(LPVOID unused);
int m4_hip_free(void);
int m4_hip_risen(void);
int m4_selector_busy(void);
DWORD WINAPI pitch_hook(LPVOID unused);
DWORD WINAPI space_hook(LPVOID unused);
DWORD WINAPI events_hook(LPVOID unused);
DWORD WINAPI tag_hook(LPVOID unused);
DWORD WINAPI draw_hook(LPVOID unused);
DWORD WINAPI fire_hook(LPVOID unused);
DWORD WINAPI load_hook(LPVOID unused);
DWORD WINAPI torch_hook(LPVOID unused);
DWORD WINAPI sight_hook(LPVOID unused);
DWORD WINAPI precision_hook(LPVOID unused);
DWORD WINAPI damage_hook(LPVOID unused);
DWORD WINAPI qam_hook(LPVOID unused);
DWORD WINAPI look_hook(LPVOID unused);
extern volatile LONG m4_precision, m4_saddle_lift, m4_saddle_push, m4_saddle_ease, m4_saddle_rate, m4_damage_fixed, m4_attack_hundredths;
extern volatile LONG m4_armour_near, m4_armour_far, m4_armour_floor, m4_damage_percent;
extern volatile LONG m4_hip_right, m4_hip_ahead, m4_hip_up, m4_aim_depth;
extern volatile LONG m4_pin_body;
extern volatile LONG m4_hit_gain;
extern volatile LONG m4_selector_volume;
extern volatile LONG m4_sway_move, m4_sway_tilt, m4_sway_lean, m4_sway_look, m4_sway_aim, m4_sway_hz;
void m4_tag_frame(void);
DWORD WINAPI profile_hook(LPVOID unused);
extern volatile LONG m4_profile;
static FILE *trace;

void note(const char *text) {
    if (!trace) return;
    SYSTEMTIME now;
    GetLocalTime(&now);
    fprintf(trace, "%02d:%02d:%02d.%03d %s\n", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, text);
    fflush(trace);
}

static void send_button(DWORD flag) {
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flag;
    SendInput(1, &input, sizeof(input));
}

static DWORD WINAPI escape_tap(LPVOID unused) {
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = VK_ESCAPE;
    input.ki.wScan = (WORD)MapVirtualKeyA(VK_ESCAPE, MAPVK_VK_TO_VSC);
    SendInput(1, &input, sizeof(input));
    Sleep(40);
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(input));
    note("escape tapped for the mod's Lua");
    return 0;
}

void m4_tap_escape(void) {
    HANDLE thread = CreateThread(NULL, 0, escape_tap, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
}

static void tap_aim(void) {
    if (!(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) return;
    send_button(MOUSEEVENTF_RIGHTUP);
    Sleep(40);
    send_button(MOUSEEVENTF_RIGHTDOWN);
    note("aim tapped to charge again");
}

static void send_move(int dx, int dy) {
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    SendInput(1, &input, sizeof(input));
}

static double milliseconds(void);
int m4_view_pitch(double *pitch);
static HANDLE own_timer(void);
static void wait_until(HANDLE timer, double deadline);

static recoil_state recoil;
static CRITICAL_SECTION kick_lock;
static HANDLE recoil_wake;

static recoil_tuning live_tuning(void) {
    recoil_tuning t = RECOIL_CS_TUNING;
    t.camera_scale = RECOIL_CAMERA_PERCENT / 100.0;
    t.shake_scale = RECOIL_SHAKE_PERCENT / 100.0;
    t.counts_per_degree = RECOIL_COUNTS_PER_DEGREE_HUNDREDTHS / 100.0;
    t.cycle_ms = RECOIL_CYCLE_MS;
    t.settle_deg_s = RECOIL_SETTLE_PER_S / t.counts_per_degree;
    t.ceiling_deg = RECOIL_CEILING_TENTHS / 10.0;
    t.bullet_scale = RECOIL_BULLET_PERCENT / 100.0;
    t.inaccuracy_scale = RECOIL_CONE_PERCENT / 100.0;
    return t;
}

#define RECOIL_OFFSETS 8
#define RECOIL_OFFSET_FRESH_MS 300
static struct { double at, up, right; } offsets[RECOIL_OFFSETS];
static int offset_first, offset_count;

int m4_recoil_take_offset(double *up, double *right) {
    double now = milliseconds();
    int found = 0;
    EnterCriticalSection(&kick_lock);
    while (offset_count && !found) {
        int i = offset_first;
        offset_first = (offset_first + 1) % RECOIL_OFFSETS;
        offset_count--;
        if (now - offsets[i].at > RECOIL_OFFSET_FRESH_MS) continue;
        *up = offsets[i].up;
        *right = offsets[i].right;
        found = 1;
    }
    LeaveCriticalSection(&kick_lock);
    return found;
}

static void kick(void) {
    if (!recoil_on) return;
    recoil_tuning t = live_tuning();
    double now = milliseconds(), pitch = 0;
    int pitched = m4_view_pitch(&pitch);
    EnterCriticalSection(&kick_lock);
    recoil_angle bullet = recoil_fire(&recoil, &t, now, pitched, pitch);
    if (offset_count == RECOIL_OFFSETS) {
        offset_first = (offset_first + 1) % RECOIL_OFFSETS;
        offset_count--;
    }
    int slot = (offset_first + offset_count++) % RECOIL_OFFSETS;
    offsets[slot].at = now;
    offsets[slot].up = -bullet.pitch;
    offsets[slot].right = -bullet.yaw;
    LeaveCriticalSection(&kick_lock);
    SetEvent(recoil_wake);
}

static volatile LONG player_dy, injected_dy;

static LRESULT CALLBACK track_physical_button(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION) {
        MSLLHOOKSTRUCT *event = (MSLLHOOKSTRUCT *)data;
        if (message == WM_MOUSEMOVE) {
            POINT at;
            if (GetCursorPos(&at)) InterlockedExchangeAdd(event->flags & LLMHF_INJECTED ? &injected_dy : &player_dy, event->pt.y - at.y);
        }
        if (!(event->flags & LLMHF_INJECTED) && message == WM_RBUTTONDOWN) {
            InterlockedExchange(&physical_aim, 1);
            if (armed) aim_pressed_at = milliseconds();
            SetEvent(engage);
            if (hip.down) {
                InterlockedExchange(&hip_takeover, 1);
                return 1;
            }
        } else if (!(event->flags & LLMHF_INJECTED) && message == WM_RBUTTONUP) {
            InterlockedExchange(&physical_aim, 0);
            aim_pressed_at = 0;
        }
        if (!(event->flags & LLMHF_INJECTED) && (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)) {
            InterlockedExchange(&physical_down, message == WM_LBUTTONDOWN);
            if (message == WM_LBUTTONDOWN && !m4_selector_busy()) {
                click_at = milliseconds();
                InterlockedIncrement(&clicks);
            }
            SetEvent(engage);
            if (armed) return 1;
        }
    }
    return CallNextHookEx(NULL, code, message, data);
}

static int game_in_front(void) {
    DWORD process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    return process == GetCurrentProcessId();
}

int m4_sight_key_route(unsigned vk, int down, int (*in_front)(void));

static LRESULT CALLBACK track_selector_key(int code, WPARAM message, LPARAM data) {
    static int held;
    KBDLLHOOKSTRUCT *event = (KBDLLHOOKSTRUCT *)data;
    int down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    if (code == HC_ACTION && !(event->flags & LLKHF_INJECTED) && m4_sight_key_route(event->vkCode, down, game_in_front)) return 1;
    if (code == HC_ACTION && !(event->flags & LLKHF_INJECTED) && (LONG)event->vkCode == SELECTOR_KEY) {
        if (down && !held && armed && game_in_front()) {
            char line[64];
            snprintf(line, sizeof(line), "selector: key pressed, press %ld", (long)InterlockedIncrement(&m4_selector_presses));
            note(line);
        }
        held = down;
    }
    return CallNextHookEx(NULL, code, message, data);
}

static DWORD WINAPI hook_thread(LPVOID unused) {
    (void)unused;
    if (!SetWindowsHookExA(WH_MOUSE_LL, track_physical_button, GetModuleHandleA(NULL), 0)) {
        note("mouse hook failed");
        return 1;
    }
    note("mouse hook installed");
    note(SetWindowsHookExA(WH_KEYBOARD_LL, track_selector_key, GetModuleHandleA(NULL), 0) ? "fire selector key hook installed"
                                                                                       : "fire selector key hook failed");
    MSG message;
    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    return 0;
}

static int newest_flag(const char *text, const char *prefix) {
    const char *newest = NULL, *hit;
    for (const char *cursor = text; (hit = strstr(cursor, prefix)); cursor = hit + 1) newest = hit;
    if (!newest) return -1;
    char digit = newest[strlen(prefix)];
    return digit >= '0' && digit <= '9' ? digit - '0' : -1;
}

static long newest_number(const char *text, const char *prefix) {
    const char *newest = NULL, *hit;
    for (const char *cursor = text; (hit = strstr(cursor, prefix)); cursor = hit + 1) newest = hit;
    if (!newest) return -1;
    const char *digits = newest + strlen(prefix);
    if (*digits < '0' || *digits > '9') return -1;
    return strtol(digits, NULL, 10);
}

int m4_mag_empty(void) {
    return InterlockedCompareExchange(&mag_left, 0, 0) == 0;
}

static void apply_newest_flags(const char *text) {
    long rounds = newest_number(text, MAG_PREFIX);
    LONG before = rounds >= 0 ? InterlockedExchange(&mag_left, rounds) : rounds;
    if (rounds >= 0 && before != rounds) {
        if (before >= 0 && rounds > before) {
            EnterCriticalSection(&kick_lock);
            recoil_reset_index(&recoil);
            LeaveCriticalSection(&kick_lock);
        }
        char line[64];
        snprintf(line, sizeof(line), "mag: %ld rounds", rounds);
        note(line);
    }
    long percent = newest_number(text, DAMAGE_PREFIX);
    if (percent > 0 && InterlockedExchange(&m4_damage_percent, percent) != percent) {
        char line[64];
        snprintf(line, sizeof(line), "damage: M4 hits deal %ld%% damage", percent);
        note(line);
    }
    int next = newest_flag(text, ARMED_PREFIX);
    if (next >= 0 && InterlockedExchange(&armed, next) != next) {
        note(next ? "armed" : "disarmed");
        if (next) SetEvent(engage);
    }
    static const char *aim_names[] = {"aim none", "aim raising", "aim ready"};
    next = newest_flag(text, AIM_PREFIX);
    if (next >= AIM_NONE && next <= AIM_READY && InterlockedExchange(&aim, next) != next) {
        aim_since = milliseconds();
        note(aim_names[next]);
        SetEvent(engage);
    }
    next = newest_flag(text, LOOK_PREFIX);
    if (next >= 0 && next <= 1 && InterlockedExchange(&m4_look_off, next) != next)
        note(next ? "look off for the player" : "look back on for the player");
    next = newest_flag(text, RECOIL_PREFIX);
    if (next >= 0 && next <= 1 && InterlockedExchange(&recoil_on, next) != next)
        note(next ? "recoil on" : "recoil off");
    next = newest_flag(text, SINGLE_PREFIX);
    if (next >= 0 && next <= 1 && InterlockedExchange(&single_shot, next) != next) {
        note(next ? "single shot: one round per trigger press" : "full auto: a held trigger repeats");
        SetEvent(engage);
    }
    next = newest_flag(text, RECHARGE_PREFIX);
    if (next >= 0) {
        LONG seen = InterlockedExchange(&recharge, next);
        if (seen >= 0 && seen != next) tap_aim();
    }
}

static void apply_setting(const char *name, long value) {
    struct { const char *name; volatile LONG *slot; } table[] = {
        {"period", &REPEAT_PERIOD_MS}, {"gap", &RELEASE_GAP_MS}, {"recoil", &RECOIL_CAMERA_PERCENT},
        {"shake", &RECOIL_SHAKE_PERCENT}, {"cpd", &RECOIL_COUNTS_PER_DEGREE_HUNDREDTHS}, {"cycle", &RECOIL_CYCLE_MS}, {"settle", &RECOIL_SETTLE_PER_S}, {"ceiling", &RECOIL_CEILING_TENTHS}, {"bullet", &RECOIL_BULLET_PERCENT}, {"cone", &RECOIL_CONE_PERCENT}, {"volume", &SHOT_VOLUME_PERCENT},
        {"precision", &m4_precision}, {"lift", &m4_saddle_lift}, {"push", &m4_saddle_push},
        {"ease", &m4_saddle_ease}, {"rate", &m4_saddle_rate},
        {"damage", &m4_damage_fixed}, {"attack", &m4_attack_hundredths},
        {"near", &m4_armour_near}, {"far", &m4_armour_far}, {"floor", &m4_armour_floor},
        {"hipright", &m4_hip_right}, {"hipahead", &m4_hip_ahead}, {"hipup", &m4_hip_up}, {"aimdepth", &m4_aim_depth},
        {"hitgain", &m4_hit_gain}, {"pinbody", &m4_pin_body}, {"swaymove", &m4_sway_move}, {"swaytilt", &m4_sway_tilt}, {"swaylean", &m4_sway_lean}, {"swaylook", &m4_sway_look}, {"swayaim", &m4_sway_aim}, {"swayhz", &m4_sway_hz},
        {"selector", &SELECTOR_KEY}, {"selectorvol", &m4_selector_volume}, {"profile", &m4_profile},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i)
        if (strcmp(name, table[i].name) == 0) InterlockedExchange(table[i].slot, value);
}

static DWORD WINAPI follow_settings(LPVOID unused) {
    (void)unused;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\m4_autofire.ini", game_root);
    FILETIME seen = {0, 0};
    HANDLE timer = own_timer();
    for (;;) {
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (GetFileAttributesExA(path, GetFileExInfoStandard, &info) &&
            CompareFileTime(&info.ftLastWriteTime, &seen) != 0) {
            seen = info.ftLastWriteTime;
            FILE *settings = fopen(path, "r");
            if (settings) {
                char line[288], name[64];
                long value;
                while (fgets(line, sizeof(line), settings))
                    if (sscanf(line, "%63[a-z]=%ld", name, &value) == 2) apply_setting(name, value);
                fclose(settings);
                snprintf(line, sizeof(line), "settings period %ld gap %ld recoil %ld shake %ld cpd %ld cycle %ld settle %ld ceiling %ld bullet %ld cone %ld volume %ld precision %ld lift %ld push %ld ease %ld rate %ld damage %ld attack %ld near %ld far %ld floor %ld selector %ld",
                         (long)REPEAT_PERIOD_MS, (long)RELEASE_GAP_MS, (long)RECOIL_CAMERA_PERCENT,
                         (long)RECOIL_SHAKE_PERCENT, (long)RECOIL_COUNTS_PER_DEGREE_HUNDREDTHS, (long)RECOIL_CYCLE_MS, (long)RECOIL_SETTLE_PER_S, (long)RECOIL_CEILING_TENTHS, (long)RECOIL_BULLET_PERCENT, (long)RECOIL_CONE_PERCENT,
                         (long)SHOT_VOLUME_PERCENT, (long)m4_precision, (long)m4_saddle_lift, (long)m4_saddle_push, (long)m4_saddle_ease, (long)m4_saddle_rate,
                         (long)m4_damage_fixed, (long)m4_attack_hundredths, (long)m4_armour_near, (long)m4_armour_far, (long)m4_armour_floor, (long)SELECTOR_KEY);
                note(line);
            }
        }
        wait_until(timer, milliseconds() + 250);
    }
    return 0;
}

static DWORD WINAPI follow_game_log(LPVOID unused) {
    (void)unused;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\kcd.log", game_root);
    LARGE_INTEGER offset = {0};
    int started = 0;
    static char buffer[65536];
    static char joined[65536 + 256];
    char carry[256] = "";
    for (;;) {
        HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER size;
            GetFileSizeEx(file, &size);
            if (!started || size.QuadPart < offset.QuadPart) {
                offset = size;
                started = 1;
            }
            SetFilePointerEx(file, offset, NULL, FILE_BEGIN);
            DWORD read;
            while (ReadFile(file, buffer, sizeof(buffer) - 1, &read, NULL) && read > 0) {
                buffer[read] = 0;
                offset.QuadPart += read;
                snprintf(joined, sizeof(joined), "%s%s", carry, buffer);
                apply_newest_flags(joined);
                size_t length = strlen(joined);
                size_t keep = length < sizeof(carry) - 1 ? length : sizeof(carry) - 1;
                memcpy(carry, joined + length - keep, keep);
                carry[keep] = 0;
            }
            CloseHandle(file);
        }
        Sleep(LOG_POLL_MS);
    }
    return 0;
}

static double milliseconds(void) {
    LARGE_INTEGER ticks;
    QueryPerformanceCounter(&ticks);
    return (double)ticks.QuadPart * 1000.0 / (double)frequency.QuadPart;
}

static double press_at[256];
static double press_after_shot[256];
static int press_count;

static void report_presses(void) {
    char line[2800];
    int used = snprintf(line, sizeof(line), "burst %d presses, gaps ms:", press_count);
    for (int i = 1; i < press_count && used < (int)sizeof(line) - 8; ++i)
        used += snprintf(line + used, sizeof(line) - used, " %.1f", press_at[i] - press_at[i - 1]);
    used += snprintf(line + used, sizeof(line) - used, "; after shot ms:");
    for (int i = 0; i < press_count && used < (int)sizeof(line) - 8; ++i)
        used += snprintf(line + used, sizeof(line) - used, " %.1f", press_after_shot[i]);
    press_count = 0;
    note(line);
}

static HANDLE own_timer(void) {
    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION_FLAG, TIMER_ALL_ACCESS);
    if (!timer) {
        timer = CreateWaitableTimerW(NULL, FALSE, NULL);
        note("high resolution timer unavailable, using the plain one");
    }
    return timer;
}

static void wait_until(HANDLE timer, double deadline) {
    double delta = deadline - milliseconds();
    if (delta <= 0) return;
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)(delta * 10000.0);
    if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) WaitForSingleObject(timer, INFINITE);
}

static void note_burst_end(const recoil_state *ended, double started_at) {
    char line[160];
    snprintf(line, sizeof(line), "recoil: %d shots, settled %.0f ms after the first, recoil index %.1f, %s", ended->shots,
             milliseconds() - started_at, ended->index,
             !ended->home_known ? "no view pitch, full return" : ended->stopped_at_home ? "return stopped at the start pitch" : "full return");
    note(line);
}

#define TRACE_ROWS 4096
#define TRACE_BURSTS 12

typedef struct {
    float at, pitch, sent, budget, floor, anchor;
    int pitched, lag, released, shots, sent_dy, player_dy, injected_dy;
} trace_row;

static trace_row trace_rows[TRACE_ROWS];
static int trace_count;
static int traced_bursts;

static void trace_step(const recoil_state *s, double since, int pitched, double pitch, int move_up) {
    if (trace_count >= TRACE_ROWS || traced_bursts >= TRACE_BURSTS) return;
    trace_rows[trace_count++] = (trace_row){(float)since, (float)pitch, (float)s->sent_up, (float)s->budget, (float)s->hand_floor,
                                            (float)s->hand_anchor, pitched, s->lag_steps, s->released, s->shots, move_up,
                                            InterlockedExchange(&player_dy, 0), InterlockedExchange(&injected_dy, 0)};
}

static void trace_dump(void) {
    if (!trace || traced_bursts >= TRACE_BURSTS || !trace_count) return;
    traced_bursts++;
    fprintf(trace, "rtrace begin %d rows: ms pitched pitch sent budget floor anchor lag released shots sent_dy player_dy injected_dy\n", trace_count);
    for (int i = 0; i < trace_count; i++) {
        const trace_row *r = &trace_rows[i];
        fprintf(trace, "rtrace %.1f %d %.3f %.3f %.3f %.3f %.3f %d %d %d %d %d %d\n", r->at, r->pitched, r->pitch, r->sent, r->budget, r->floor,
                r->anchor, r->lag, r->released, r->shots, r->sent_dy, r->player_dy, r->injected_dy);
    }
    fprintf(trace, "rtrace end\n");
    fflush(trace);
    trace_count = 0;
}

static DWORD WINAPI apply_recoil(LPVOID unused) {
    (void)unused;
    HANDLE timer = own_timer();
    double started_at = 0;
    int was_busy = 0;
    for (;;) {
        double now = milliseconds();
        int move_side = 0, move_up = 0;
        recoil_tuning t = live_tuning();
        double pitch = 0;
        int pitched = m4_view_pitch(&pitch);
        EnterCriticalSection(&kick_lock);
        recoil_step(&recoil, &t, now, pitched, pitch, &move_side, &move_up);
        int busy = !recoil.idle;
        if (busy) {
            if (!was_busy) {
                trace_count = 0;
                InterlockedExchange(&player_dy, 0);
                InterlockedExchange(&injected_dy, 0);
            }
            trace_step(&recoil, was_busy ? now - started_at : 0, pitched, pitch, move_up);
        }
        recoil_state ended = recoil;
        if (!busy) recoil.shots = 0;
        LeaveCriticalSection(&kick_lock);
        if (move_up || move_side) send_move(move_side, move_up);
        if (busy && !was_busy) started_at = now;
        if (!busy && was_busy) {
            note_burst_end(&ended, started_at);
            trace_dump();
        }
        was_busy = busy;
        if (!busy) {
            WaitForSingleObject(recoil_wake, INFINITE);
            continue;
        }
        wait_until(timer, now + RECOIL_STEP_MS);
    }
    return 0;
}

// The mod's Lua plays the frame mark once a frame while the M4 is in hand.
void m4_frame(void) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    LONG64 last = InterlockedExchange64(&frame_tick, now.QuadPart);
    double step = last ? (double)(now.QuadPart - last) * 1000.0 / frequency.QuadPart : 0;
    if (step > 0 && step < 100) frame_time = frame_time ? frame_time * 0.9 + step * 0.1 : step;
    InterlockedIncrement(&frames);
    SetEvent(engage);
    m4_tag_frame();
}

void m4_clock_ready(const char *wav);
int m4_clock_shot(long period_ms, long volume_percent);
void m4_clock_trigger(int held);

void m4_in_hand(void) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\m4_shot.wav", game_root);
    m4_clock_ready(path);
}

long m4_period_ms(void) {
    return REPEAT_PERIOD_MS;
}

int m4_aim_held(void) {
    return aim != AIM_NONE;
}

int m4_hip_held(void) {
    return hip.down;
}

static volatile LONG hip_shown;

int m4_hip_state(void) {
    return hip_shown;
}

double m4_since_shot_ms(void) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    LONG64 shot = InterlockedCompareExchange64(&shot_tick, 0, 0);
    return shot ? (double)(now.QuadPart - shot) * 1000 / frequency.QuadPart : 1e12;
}

int m4_shot_fired(void) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    InterlockedExchange(&shot_frame, frames);
    InterlockedExchange64(&shot_tick, now.QuadPart);
    LONG left = InterlockedCompareExchange(&mag_left, 0, 0);
    while (left > 0) {
        LONG seen = InterlockedCompareExchange(&mag_left, left - 1, left);
        if (seen == left) break;
        left = seen;
    }
    note("shot fired");
    if (armed) kick();
    SetEvent(engage);
    return m4_clock_shot(REPEAT_PERIOD_MS, SHOT_VOLUME_PERCENT);
}

static DWORD WINAPI repeat_presses(LPVOID unused) {
    (void)unused;
    HANDLE timer = own_timer();
    Cadence state = {0};
    double observed = 0;
    for (;;) {
        double now = milliseconds();
        double shot_at = (double)InterlockedCompareExchange64(&shot_tick, 0, 0) * 1000 / frequency.QuadPart;
        if (shot_at > observed) {
            char line[160];
            snprintf(line, sizeof(line), "shot: confirmed qpc_ms=%.3f interval_ms=%.3f frame=%ld frame_ms=%.2f",
                     shot_at, observed ? shot_at - observed : 0, (long)shot_frame, frame_time);
            note(line);
            observed = shot_at;
        }
        int was_active = state.active;
        int physical = physical_down && !m4_selector_busy(), enabled = armed;
        double ticked = (double)InterlockedCompareExchange64(&frame_tick, 0, 0) * 1000 / frequency.QuadPart;
        double aim_stamp = aim_pressed_at;
        int effective_aim = aim != AIM_READY && aim_stamp != 0 && now - aim_stamp <= CLICK_LATCH_MS ? AIM_RAISING : aim;
        if (effective_aim == AIM_RAISING && shot_at > (aim_stamp ? aim_stamp : aim_since)) effective_aim = AIM_READY;
        int hip_allowed = enabled && aim == AIM_NONE && m4_hip_free(), hip_free = hip_allowed && !physical_aim;
        int hip_risen = m4_hip_risen(), hip_ready = hip_free && hip_risen;
        effective_aim = hip_aim(effective_aim, hip_free, hip_risen, hip.down);
        double charged_at = (double)InterlockedCompareExchange64(&charged_tick, 0, 0) * 1000 / frequency.QuadPart;
        CadenceInput input = {.now = now, .shot_at = shot_at, .frame_ms = now - ticked < 200 ? frame_time : 0, .charged_at = charged_at, .click_at = click_at,
                              .frame = frames, .shot_frame = shot_frame, .clicks = clicks,
                              .physical = physical, .enabled = enabled, .aim = effective_aim, .single = single_shot,
                              .game_down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0};
        int action = cadence_step(&state, &input, REPEAT_PERIOD_MS, RELEASE_GAP_MS);
        m4_clock_trigger(state.active && enabled && !state.spent);
        if (action != CADENCE_NONE) {
            send_button(action == CADENCE_UP ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_LEFTDOWN);
            if (action == CADENCE_DOWN && enabled && press_count < (int)(sizeof(press_at) / sizeof(press_at[0]))) {
                press_after_shot[press_count] = observed ? milliseconds() - observed : -1;
                press_at[press_count++] = milliseconds();
            }
        }
        if (was_active && !state.active && press_count) report_presses();
        if (InterlockedExchange(&hip_takeover, 0)) {
            int taken = hip.down;
            hip.down = 0;
            hip.released_at = now;
            if (taken) {
                send_button(MOUSEEVENTF_RIGHTUP);
                Sleep(40);
            }
            if (physical_aim) send_button(MOUSEEVENTF_RIGHTDOWN);
            note("hip: the aim takes over the hip shot");
        }
        int hip_action = hip_step(&hip, now, shot_at, hip.down ? hip_allowed : hip_ready, physical || state.latched);
        InterlockedExchange(&hip_shown, hip_state(hip_free, hip.down, physical || state.latched));
        if (hip_action != HIP_NONE) {
            send_button(hip_action == HIP_PRESS ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP);
            note(hip_action == HIP_PRESS ? "hip: charge held for a hip shot" : "hip: charge let go");
        }
        if (state.active || hip.down) wait_until(timer, now + 2);
        else WaitForSingleObject(engage, 50);
    }
    return 0;
}

static DWORD WINAPI start(LPVOID unused) {
    (void)unused;
    GetModuleFileNameA(NULL, game_root, sizeof(game_root));
    for (int level = 0; level < 3; ++level) {
        char *slash = strrchr(game_root, '\\');
        if (slash) *slash = 0;
    }
    char trace_path[MAX_PATH];
    snprintf(trace_path, sizeof(trace_path), "%s\\m4_autofire.log", game_root);
    trace = fopen(trace_path, "w");
    QueryPerformanceFrequency(&frequency);
    timeBeginPeriod(1);
    engage = CreateEventW(NULL, FALSE, FALSE, NULL);
    recoil_wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    InitializeCriticalSection(&kick_lock);
    recoil_init(&recoil, &RECOIL_M4A4);
    note("m4_autofire loaded");
    CreateThread(NULL, 0, hook_thread, NULL, 0, NULL);
    CreateThread(NULL, 0, follow_game_log, NULL, 0, NULL);
    CreateThread(NULL, 0, repeat_presses, NULL, 0, NULL);
    CreateThread(NULL, 0, apply_recoil, NULL, 0, NULL);
    CreateThread(NULL, 0, follow_settings, NULL, 0, NULL);
    CreateThread(NULL, 0, ammo_hook, NULL, 0, NULL);
    CreateThread(NULL, 0, sound_hook, NULL, 0, NULL);
    CreateThread(NULL, 0, impact_hook, NULL, 0, NULL);
    CreateThread(NULL, 0, mass_hook, NULL, 0, NULL);
    CreateThread(NULL, 0, hip_hook, NULL, 0, NULL);
    CreateThread(NULL, 0, pitch_hook, NULL, 0, NULL);
    CreateThread(NULL, 0, space_hook, NULL, 0, NULL);
    CreateThread(NULL, 0, events_hook, NULL, 0, NULL);
    CreateThread(NULL, 0, tag_hook, NULL, 0, NULL);
    CreateThread(NULL, 0, draw_hook, NULL, 0, NULL);
    CreateThread(NULL, 0, fire_hook, NULL, 0, NULL);
    CreateThread(NULL, 0, qam_hook, NULL, 0, NULL);
    CreateThread(NULL, 0, load_hook, NULL, 0, NULL);
    CreateThread(NULL, 0, torch_hook, NULL, 0, NULL);
    CreateThread(NULL, 0, sight_hook, NULL, 0, NULL);
    CreateThread(NULL, 0, precision_hook, NULL, 0, NULL);
    CreateThread(NULL, 0, damage_hook, NULL, 0, NULL);
    CreateThread(NULL, 0, look_hook, NULL, 0, NULL);
    CreateThread(NULL, 0, profile_hook, NULL, 0, NULL);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        CreateThread(NULL, 0, start, NULL, 0, NULL);
    }
    return TRUE;
}
