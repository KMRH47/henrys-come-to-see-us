#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

void note(const char *text);
int m4_aim_held(void);
int m4_stance(void);
unsigned char *near_page(unsigned char *base);
unsigned char *emit_jump(unsigned char *at, void *target);
void put_call(unsigned char *from, unsigned char *to, size_t length);

#define PITCH_CLAMP_RVA 0x53b508
#define STANCE_CROUCHED 1
#define STANCE_MOUNTED 2
#define CROUCHED_AIM_UP_LIMIT 0.82030475f
#define LOGGED_CAPS 4

static const unsigned int PITCH_CLAMP_CALLS[] = {0x4419f9, 0x285ec2b};
static const unsigned char PITCH_CLAMP_BYTES[] = {
    0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56
};

void *m4_pitch_original;
static volatile LONG logged;

void m4_pitch_cap(float *angles) {
    if (!angles || angles[0] <= CROUCHED_AIM_UP_LIMIT || !m4_aim_held()) return;
    int stance = m4_stance();
    if (!(stance & STANCE_CROUCHED) || (stance & STANCE_MOUNTED)) return;
    if (InterlockedIncrement(&logged) <= LOGGED_CAPS) {
        char line[96];
        snprintf(line, sizeof(line), "pitch: crouched aim held at 47 deg up, asked for %.1f deg", angles[0] * 57.29578f);
        note(line);
    }
    angles[0] = CROUCHED_AIM_UP_LIMIT;
}

__attribute__((naked)) static void clamp(void) {
    __asm__ volatile(
        "push %rdx\n"
        "sub $0x20, %rsp\n"
        "call *m4_pitch_original(%rip)\n"
        "add $0x20, %rsp\n"
        "mov (%rsp), %rcx\n"
        "push %rcx\n push %rdx\n push %r8\n push %r9\n push %r10\n push %r11\n push %rax\n"
        "sub $0x88, %rsp\n"
        "movdqu %xmm0, 0x20(%rsp)\n movdqu %xmm1, 0x30(%rsp)\n movdqu %xmm2, 0x40(%rsp)\n"
        "movdqu %xmm3, 0x50(%rsp)\n movdqu %xmm4, 0x60(%rsp)\n movdqu %xmm5, 0x70(%rsp)\n"
        "call m4_pitch_cap\n"
        "movdqu 0x20(%rsp), %xmm0\n movdqu 0x30(%rsp), %xmm1\n movdqu 0x40(%rsp), %xmm2\n"
        "movdqu 0x50(%rsp), %xmm3\n movdqu 0x60(%rsp), %xmm4\n movdqu 0x70(%rsp), %xmm5\n"
        "add $0x88, %rsp\n"
        "pop %rax\n pop %r11\n pop %r10\n pop %r9\n pop %r8\n pop %rdx\n pop %rcx\n"
        "add $8, %rsp\n"
        "ret\n");
}

static int pitch_install(unsigned char *game) {
    if (memcmp(game + PITCH_CLAMP_RVA, PITCH_CLAMP_BYTES, sizeof(PITCH_CLAMP_BYTES))) {
        note("pitch: clamp signature mismatch; crouched aim keeps the engine limit");
        return 0;
    }
    for (size_t i = 0; i < sizeof(PITCH_CLAMP_CALLS) / sizeof(PITCH_CLAMP_CALLS[0]); i++) {
        unsigned char *call = game + PITCH_CLAMP_CALLS[i];
        if (call[0] != 0xe8 || call + 5 + *(INT32 *)(call + 1) != game + PITCH_CLAMP_RVA) {
            note("pitch: clamp call mismatch; crouched aim keeps the engine limit");
            return 0;
        }
    }
    unsigned char *entry = near_page(game);
    if (!entry) {
        note("pitch: no nearby page; crouched aim keeps the engine limit");
        return 0;
    }
    m4_pitch_original = game + PITCH_CLAMP_RVA;
    emit_jump(entry, (void *)clamp);
    FlushInstructionCache(GetCurrentProcess(), entry, 14);
    for (size_t i = 0; i < sizeof(PITCH_CLAMP_CALLS) / sizeof(PITCH_CLAMP_CALLS[0]); i++)
        put_call(game + PITCH_CLAMP_CALLS[i], entry, 5);
    note("pitch: crouched aim stops at 47 deg up");
    return 1;
}

DWORD WINAPI pitch_hook(LPVOID unused) {
    (void)unused;
    HMODULE game;
    while (!(game = GetModuleHandleA("WHGame.dll"))) Sleep(1);
    pitch_install((unsigned char *)game);
    return 0;
}
