#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

void note(const char *text);
double m4_since_shot_ms(void);
unsigned char *near_page(unsigned char *base);
unsigned char *emit_jump(unsigned char *at, void *target);
void put_call(unsigned char *from, unsigned char *to, size_t length);

#define LAUNCH_RVA 0x2aad50c
#define PARTICLE_PARAMS_RVA 0xb35050
#define PE_PARTICLE 5
#define PE_TYPE_SLOT 1
#define PE_SET_PARAMS_SLOT 4
#define PARTICLE_MASS 8
#define M4_ROUND_MASS 0.01f
#define M4_LAUNCH_WINDOW_MS 1500
#define LOGGED_LAUNCHES 4

static const unsigned int LAUNCH_CALLS[] = {0x2aa8901, 0x2aad4ff, 0x2acdab0};
static const unsigned char LAUNCH_BYTES[] = {
    0x48, 0x85, 0xd2, 0x0f, 0x84, 0xbc, 0x01, 0x00, 0x00, 0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08
};

void *m4_launch_original;
static void *(*particle_params)(unsigned char *);
static volatile LONG logged;

void m4_round_launch(void **entity, int thread, float speed) {
    double since = m4_since_shot_ms();
    if (!entity || since >= M4_LAUNCH_WINDOW_MS) return;
    void **vtable = *(void ***)entity;
    if (((int (*)(void *))vtable[PE_TYPE_SLOT])(entity) != PE_PARTICLE) return;
    unsigned char params[0xb0];
    particle_params(params);
    *(float *)(params + PARTICLE_MASS) = M4_ROUND_MASS;
    ((int (*)(void *, void *, int))vtable[PE_SET_PARAMS_SLOT])(entity, params, thread);
    if (InterlockedIncrement(&logged) <= LOGGED_LAUNCHES) {
        char line[128];
        snprintf(line, sizeof(line), "mass: M4 round %.0f m/s weighs %.3f kg, %.0f ms after the shot", speed, M4_ROUND_MASS, since);
        note(line);
    }
}

__attribute__((naked)) static void launch(void) {
    __asm__ volatile(
        "push %rcx\n push %rdx\n push %r8\n push %r9\n push %r10\n push %r11\n push %rax\n"
        "sub $0x80, %rsp\n"
        "movdqu %xmm0, 0x20(%rsp)\n movdqu %xmm1, 0x30(%rsp)\n movdqu %xmm2, 0x40(%rsp)\n"
        "movdqu %xmm3, 0x50(%rsp)\n movdqu %xmm4, 0x60(%rsp)\n movdqu %xmm5, 0x70(%rsp)\n"
        "mov %rdx, %rcx\n mov 0xe8(%rsp), %edx\n movaps %xmm3, %xmm2\n"
        "call m4_round_launch\n"
        "movdqu 0x20(%rsp), %xmm0\n movdqu 0x30(%rsp), %xmm1\n movdqu 0x40(%rsp), %xmm2\n"
        "movdqu 0x50(%rsp), %xmm3\n movdqu 0x60(%rsp), %xmm4\n movdqu 0x70(%rsp), %xmm5\n"
        "add $0x80, %rsp\n"
        "pop %rax\n pop %r11\n pop %r10\n pop %r9\n pop %r8\n pop %rdx\n pop %rcx\n"
        "jmp *m4_launch_original(%rip)\n");
}

static int mass_install(unsigned char *game) {
    if (memcmp(game + LAUNCH_RVA, LAUNCH_BYTES, sizeof(LAUNCH_BYTES))) {
        note("mass: launch signature mismatch; M4 rounds keep the arrow mass");
        return 0;
    }
    for (size_t i = 0; i < sizeof(LAUNCH_CALLS) / sizeof(LAUNCH_CALLS[0]); i++) {
        unsigned char *call = game + LAUNCH_CALLS[i];
        if (call[0] != 0xe8 || call + 5 + *(INT32 *)(call + 1) != game + LAUNCH_RVA) {
            note("mass: launch call mismatch; M4 rounds keep the arrow mass");
            return 0;
        }
    }
    unsigned char *entry = near_page(game);
    if (!entry) {
        note("mass: no nearby page; M4 rounds keep the arrow mass");
        return 0;
    }
    m4_launch_original = game + LAUNCH_RVA;
    particle_params = (void *)(game + PARTICLE_PARAMS_RVA);
    emit_jump(entry, (void *)launch);
    FlushInstructionCache(GetCurrentProcess(), entry, 14);
    for (size_t i = 0; i < sizeof(LAUNCH_CALLS) / sizeof(LAUNCH_CALLS[0]); i++)
        put_call(game + LAUNCH_CALLS[i], entry, 5);
    note("mass: M4 rounds launch at 0.01 kg");
    return 1;
}

DWORD WINAPI mass_hook(LPVOID unused) {
    (void)unused;
    HMODULE game;
    while (!(game = GetModuleHandleA("WHGame.dll"))) Sleep(1);
    mass_install((unsigned char *)game);
    return 0;
}
