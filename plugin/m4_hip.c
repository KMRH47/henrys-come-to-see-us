#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

void note(const char *text);
double m4_since_shot_ms(void);
int m4_hip_held(void);
void *m4_string_cvar(void *console, const char *name, const char *start, const char *help);
void m4_cvar_set(void *cvar, const char *text);

#define LAUNCH_ENTRY_RVA 0x2aa85a8
#define HIP_SHOT_CVAR "m4_hip_shot"
#define HIP_FREE_CVAR "m4_hip_free"
#define HIP_BUSY_CVAR "m4_hip_busy"
#define HIP_AIR_CVAR "m4_hip_air"
#define HIP_LINE_PREFIX "m4_hip_line "
#define HIP_FREE_FRESH_MS 200
#define HIP_LINE_FRESH_MS 500
#define HIP_LAUNCH_WINDOW_MS 1500
#define HIP_NEAR_M 3.0f
#define LOGGED_ROUNDS 4

static const unsigned int LAUNCH_SLOTS[] = {0x4711d28, 0x471d138};
static const unsigned char LAUNCH_ENTRY_BYTES[] = {
    0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x10, 0x48, 0x89, 0x78, 0x18, 0x4c, 0x89, 0x70, 0x20, 0x55
};

void *m4_launch_entry_original;
static void *hip_cvar;
static volatile LONG hip_registering, logged_rounds;
static volatile double hip_free_at = -1e12, hip_line_at = -1e12;
static volatile double hip_air_at = -1e12;
static float line_start[3], line_way[3], round_velocity[3];

static double hip_ms(void) {
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return (double)now.QuadPart * 1000 / frequency.QuadPart;
}

int m4_hip_free(void) {
    return hip_ms() - hip_free_at < HIP_FREE_FRESH_MS;
}

int m4_hip_air(void) {
    return hip_ms() - hip_air_at < HIP_FREE_FRESH_MS;
}

void m4_hip_console(void *console, const char *name) {
    if (!hip_cvar && console && !InterlockedCompareExchange(&hip_registering, 1, 0))
        hip_cvar = m4_string_cvar(console, HIP_SHOT_CVAR, "0", "M4 hip shot: the plugin holds the charge for it, set by m4_autofire");
    if (!name || strncmp(name, "m4_hip_", 7)) return;
    if (!strcmp(name, HIP_FREE_CVAR)) hip_free_at = hip_ms();
    else if (!strcmp(name, HIP_BUSY_CVAR)) hip_free_at = -1e12;
    else if (!strcmp(name, HIP_AIR_CVAR)) hip_air_at = hip_ms();
    else if (!strcmp(name, HIP_SHOT_CVAR)) {
        if (hip_cvar) m4_cvar_set(hip_cvar, m4_hip_held() ? "1" : "0");
    } else if (!strncmp(name, HIP_LINE_PREFIX, sizeof(HIP_LINE_PREFIX) - 1)) {
        float v[6];
        if (sscanf(name + sizeof(HIP_LINE_PREFIX) - 1, "%f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return;
        memcpy(line_start, v, sizeof(line_start));
        memcpy(line_way, v + 3, sizeof(line_way));
        hip_line_at = hip_ms();
    }
}

void m4_round_aim(const float **args) {
    const float *velocity = args[0], *way = args[1], *start = args[2];
    if (m4_since_shot_ms() >= HIP_LAUNCH_WINDOW_MS || hip_ms() - hip_line_at >= HIP_LINE_FRESH_MS) return;
    float gap = 0, speed = 0;
    for (int i = 0; i < 3; i++) {
        gap += (start[i] - line_start[i]) * (start[i] - line_start[i]);
        speed += velocity[i] * velocity[i];
    }
    if (gap > HIP_NEAR_M * HIP_NEAR_M) return;
    speed = sqrtf(speed);
    for (int i = 0; i < 3; i++) round_velocity[i] = line_way[i] * speed;
    if (InterlockedIncrement(&logged_rounds) <= LOGGED_ROUNDS) {
        char line[200];
        snprintf(line, sizeof(line), "hip: round moved %.2f m to the muzzle, %.1f deg to the view, %.0f m/s", sqrtf(gap),
                 acosf(fmaxf(-1, fminf(1, way[0] * line_way[0] + way[1] * line_way[1] + way[2] * line_way[2]))) * 57.29578f, speed);
        note(line);
    }
    args[0] = round_velocity;
    args[1] = line_way;
    args[2] = line_start;
}

__attribute__((naked)) static void launch_entry(void) {
    __asm__ volatile(
        "push %rcx\n push %rdx\n push %r8\n push %r9\n push %r10\n push %r11\n push %rax\n"
        "sub $0x80, %rsp\n"
        "movdqu %xmm0, 0x20(%rsp)\n movdqu %xmm1, 0x30(%rsp)\n movdqu %xmm2, 0x40(%rsp)\n"
        "movdqu %xmm3, 0x50(%rsp)\n movdqu %xmm4, 0x60(%rsp)\n movdqu %xmm5, 0x70(%rsp)\n"
        "lea 0x98(%rsp), %rcx\n"
        "call m4_round_aim\n"
        "movdqu 0x20(%rsp), %xmm0\n movdqu 0x30(%rsp), %xmm1\n movdqu 0x40(%rsp), %xmm2\n"
        "movdqu 0x50(%rsp), %xmm3\n movdqu 0x60(%rsp), %xmm4\n movdqu 0x70(%rsp), %xmm5\n"
        "add $0x80, %rsp\n"
        "pop %rax\n pop %r11\n pop %r10\n pop %r9\n pop %r8\n pop %rdx\n pop %rcx\n"
        "jmp *m4_launch_entry_original(%rip)\n");
}

static int hip_install(unsigned char *game) {
    if (memcmp(game + LAUNCH_ENTRY_RVA, LAUNCH_ENTRY_BYTES, sizeof(LAUNCH_ENTRY_BYTES))) {
        note("hip: launch signature mismatch; hip rounds leave along the raw gun");
        return 0;
    }
    for (size_t i = 0; i < sizeof(LAUNCH_SLOTS) / sizeof(LAUNCH_SLOTS[0]); i++) {
        if (*(unsigned char **)(game + LAUNCH_SLOTS[i]) != game + LAUNCH_ENTRY_RVA) {
            note("hip: launch vtable mismatch; hip rounds leave along the raw gun");
            return 0;
        }
    }
    m4_launch_entry_original = game + LAUNCH_ENTRY_RVA;
    for (size_t i = 0; i < sizeof(LAUNCH_SLOTS) / sizeof(LAUNCH_SLOTS[0]); i++) {
        void **slot = (void **)(game + LAUNCH_SLOTS[i]);
        DWORD old;
        VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old);
        *slot = (void *)launch_entry;
        VirtualProtect(slot, sizeof(*slot), old, &old);
    }
    note("hip: hip rounds leave the shown muzzle along the view");
    return 1;
}

DWORD WINAPI hip_hook(LPVOID unused) {
    (void)unused;
    HMODULE game;
    while (!(game = GetModuleHandleA("WHGame.dll"))) Sleep(1);
    hip_install((unsigned char *)game);
    return 0;
}
