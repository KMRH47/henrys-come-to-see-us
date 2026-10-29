#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mmsystem.h>
#include "m4_cadence.h"


#define LOG_POLL_MS 10
#define RECOIL_STEP_MS 4
#define RECOIL_MAX_ACTIVE 8
#define ARMED_PREFIX "M4CARBINE auto armed "
#define AIM_PREFIX "M4CARBINE auto aim "
#define RECHARGE_PREFIX "M4CARBINE auto recharge "
#define MAG_PREFIX "M4CARBINE auto mag "
#define LOOK_PREFIX "M4CARBINE auto look "
#define RECOIL_PREFIX "M4CARBINE auto recoil "

#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION_FLAG 0x00000002

static volatile LONG REPEAT_PERIOD_MS = 60;
static volatile LONG RELEASE_GAP_MS = 8;
static volatile LONG RECOIL_KICK = 34;
static volatile LONG RECOIL_RECOVER_PERCENT = 53;
static volatile LONG RECOIL_RISE_MS = 40;
static volatile LONG RECOIL_FALL_MS = 200;
static volatile LONG RECOIL_SPREAD = 2;
static volatile LONG RECOIL_FOLLOW_PERCENT = 20;
static volatile LONG SHOT_VOLUME_PERCENT = 50;

static volatile LONG physical_down = 0;
static volatile LONG armed = 0;
static volatile LONG aim = AIM_NONE;
static volatile LONG recharge = -1;
static volatile LONG mag_left = -1;
static volatile LONG recoil_on = 1;
volatile LONG m4_look_off;
static volatile LONG clicks = 0;
static volatile double aim_pressed_at = 0;
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
extern volatile LONG m4_armour_near, m4_armour_far, m4_armour_floor;
void m4_tag_frame(void);
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

static struct { double start; double sideways; double kick; double recover; double rise; double fall; } kicks[RECOIL_MAX_ACTIVE];
static int kick_count;
static CRITICAL_SECTION kick_lock;
static HANDLE recoil_wake;

static int wobble(void) {
    static unsigned seed = 1;
    seed = seed * 1103515245 + 12345;
    return (int)((seed >> 16) % (2 * RECOIL_SPREAD + 1)) - RECOIL_SPREAD;
}

static void kick(void) {
    if (!recoil_on) return;
    EnterCriticalSection(&kick_lock);
    if (kick_count < RECOIL_MAX_ACTIVE) {
        kicks[kick_count].start = milliseconds();
        kicks[kick_count].sideways = wobble();
        kicks[kick_count].kick = RECOIL_KICK;
        kicks[kick_count].recover = RECOIL_RECOVER_PERCENT;
        kicks[kick_count].rise = RECOIL_RISE_MS;
        kicks[kick_count].fall = RECOIL_FALL_MS;
        kick_count++;
    }
    LeaveCriticalSection(&kick_lock);
    SetEvent(recoil_wake);
}

static LRESULT CALLBACK track_physical_button(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION) {
        MSLLHOOKSTRUCT *event = (MSLLHOOKSTRUCT *)data;
        if (!(event->flags & LLMHF_INJECTED) && message == WM_RBUTTONDOWN) {
            if (armed) aim_pressed_at = milliseconds();
            SetEvent(engage);
        } else if (!(event->flags & LLMHF_INJECTED) && message == WM_RBUTTONUP) {
            aim_pressed_at = 0;
        }
        if (!(event->flags & LLMHF_INJECTED) && (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)) {
            InterlockedExchange(&physical_down, message == WM_LBUTTONDOWN);
            if (message == WM_LBUTTONDOWN) InterlockedIncrement(&clicks);
            SetEvent(engage);
            if (armed) return 1;
        }
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
    if (rounds >= 0 && InterlockedExchange(&mag_left, rounds) != rounds) {
        char line[64];
        snprintf(line, sizeof(line), "mag: %ld rounds", rounds);
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
    next = newest_flag(text, RECHARGE_PREFIX);
    if (next >= 0) {
        LONG seen = InterlockedExchange(&recharge, next);
        if (seen >= 0 && seen != next) tap_aim();
    }
}

static void apply_setting(const char *name, long value) {
    struct { const char *name; volatile LONG *slot; } table[] = {
        {"period", &REPEAT_PERIOD_MS}, {"gap", &RELEASE_GAP_MS}, {"kick", &RECOIL_KICK},
        {"recover", &RECOIL_RECOVER_PERCENT}, {"rise", &RECOIL_RISE_MS},
        {"fall", &RECOIL_FALL_MS}, {"spread", &RECOIL_SPREAD}, {"follow", &RECOIL_FOLLOW_PERCENT}, {"volume", &SHOT_VOLUME_PERCENT},
        {"precision", &m4_precision}, {"lift", &m4_saddle_lift}, {"push", &m4_saddle_push},
        {"ease", &m4_saddle_ease}, {"rate", &m4_saddle_rate},
        {"damage", &m4_damage_fixed}, {"attack", &m4_attack_hundredths},
        {"near", &m4_armour_near}, {"far", &m4_armour_far}, {"floor", &m4_armour_floor},
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
                snprintf(line, sizeof(line), "settings period %ld gap %ld kick %ld recover %ld rise %ld fall %ld spread %ld follow %ld volume %ld precision %ld lift %ld push %ld ease %ld rate %ld damage %ld attack %ld near %ld far %ld floor %ld",
                         (long)REPEAT_PERIOD_MS, (long)RELEASE_GAP_MS, (long)RECOIL_KICK,
                         (long)RECOIL_RECOVER_PERCENT, (long)RECOIL_RISE_MS, (long)RECOIL_FALL_MS, (long)RECOIL_SPREAD, (long)RECOIL_FOLLOW_PERCENT,
                         (long)SHOT_VOLUME_PERCENT, (long)m4_precision, (long)m4_saddle_lift, (long)m4_saddle_push, (long)m4_saddle_ease, (long)m4_saddle_rate,
                         (long)m4_damage_fixed, (long)m4_attack_hundredths, (long)m4_armour_near, (long)m4_armour_far, (long)m4_armour_floor);
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

static double kick_offset(int index, double age, double *sideways_share) {
    double peak = -kicks[index].kick;
    double kept = peak * (100.0 - kicks[index].recover) / 100.0;
    double rise = kicks[index].rise, fall = kicks[index].fall;
    if (age < rise) {
        *sideways_share = age / rise;
        return peak * age / rise;
    }
    *sideways_share = 1.0;
    if (age < rise + fall) return peak + (kept - peak) * (age - rise) / fall;
    return kept;
}

static DWORD WINAPI apply_recoil(LPVOID unused) {
    (void)unused;
    HANDLE timer = own_timer();
    double settled_up = 0, settled_side = 0, applied_up = 0, applied_side = 0, home_pitch = 0, pitch, held_back = 0, follow_owed = 0;
    double last = 0;
    int idle = 1, home = 0;
    for (;;) {
        EnterCriticalSection(&kick_lock);
        int active = kick_count;
        LeaveCriticalSection(&kick_lock);
        if (!active && settled_up == 0) {
            if (!idle) {
                char line[160];
                if (home && m4_view_pitch(&pitch))
                    snprintf(line, sizeof(line), "recoil: burst from pitch %.2f ended at %.2f, held back %.0f counts", home_pitch, pitch,
                             held_back);
                else
                    snprintf(line, sizeof(line), "recoil: burst without a view pitch, full return");
                note(line);
            }
            applied_up = applied_side = settled_up = settled_side = held_back = follow_owed = 0;
            idle = 1;
            WaitForSingleObject(recoil_wake, INFINITE);
            continue;
        }
        double now = milliseconds();
        if (idle) {
            idle = 0;
            home = m4_view_pitch(&home_pitch);
            last = now;
        }
        double wanted_up = 0, wanted_side = 0;
        EnterCriticalSection(&kick_lock);
        int kept = 0;
        for (int i = 0; i < kick_count; ++i) {
            double age = now - kicks[i].start, share;
            double offset = kick_offset(i, age, &share);
            if (age >= kicks[i].rise + kicks[i].fall) {
                settled_up += offset;
                settled_side += kicks[i].sideways;
                continue;
            }
            wanted_up += offset;
            wanted_side += kicks[i].sideways * share;
            kicks[kept++] = kicks[i];
        }
        kick_count = kept;
        LeaveCriticalSection(&kick_lock);
        if (!physical_down && settled_up != 0) {
            double share = (now - last) / RECOIL_FALL_MS;
            settled_up = settled_up > -0.5 || share >= 1 ? 0 : settled_up * (1 - share);
        }
        last = now;
        wanted_up += settled_up;
        wanted_side += settled_side;
        int step_up = (int)((wanted_up - applied_up) + ((wanted_up > applied_up) ? 0.5 : -0.5));
        int step_side = (int)((wanted_side - applied_side) + ((wanted_side > applied_side) ? 0.5 : -0.5));
        if (step_up > 0 && home && m4_view_pitch(&pitch) && pitch <= home_pitch) {
            follow_owed += step_up * RECOIL_FOLLOW_PERCENT / 100.0;
            int follow = (int)follow_owed;
            follow_owed -= follow;
            applied_up += step_up - follow;
            held_back += step_up - follow;
            step_up = follow;
        }
        if (step_up || step_side) {
            send_move(step_side, step_up);
            applied_up += step_up;
            applied_side += step_side;
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
        int physical = physical_down, enabled = armed;
        double ticked = (double)InterlockedCompareExchange64(&frame_tick, 0, 0) * 1000 / frequency.QuadPart;
        double aim_stamp = aim_pressed_at;
        int effective_aim = aim != AIM_READY && aim_stamp != 0 && now - aim_stamp <= CLICK_LATCH_MS ? AIM_RAISING : aim;
        if (effective_aim == AIM_RAISING && shot_at > (aim_stamp ? aim_stamp : aim_since)) effective_aim = AIM_READY;
        double charged_at = (double)InterlockedCompareExchange64(&charged_tick, 0, 0) * 1000 / frequency.QuadPart;
        CadenceInput input = {.now = now, .shot_at = shot_at, .frame_ms = now - ticked < 200 ? frame_time : 0, .charged_at = charged_at,
                              .frame = frames, .shot_frame = shot_frame, .clicks = clicks,
                              .physical = physical, .enabled = enabled, .aim = effective_aim,
                              .game_down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0};
        int action = cadence_step(&state, &input, REPEAT_PERIOD_MS, RELEASE_GAP_MS);
        m4_clock_trigger(state.active && enabled);
        if (action != CADENCE_NONE) {
            send_button(action == CADENCE_UP ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_LEFTDOWN);
            if (action == CADENCE_DOWN && enabled && press_count < (int)(sizeof(press_at) / sizeof(press_at[0]))) {
                press_after_shot[press_count] = observed ? milliseconds() - observed : -1;
                press_at[press_count++] = milliseconds();
            }
        }
        if (was_active && !state.active && press_count) report_presses();
        if (state.active) wait_until(timer, now + 2);
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
