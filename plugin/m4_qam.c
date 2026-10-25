#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

void note(const char *text);
unsigned char *near_page(unsigned char *base);
unsigned char *emit_jump(unsigned char *at, void *target);
void put_call(unsigned char *from, unsigned char *to, size_t length);
void *m4_string_cvar(void *console, const char *name, const char *start, const char *help);
void m4_cvar_set(void *cvar, const char *text);

#define QAM_SET_ARRAY_CALL_RVA 0xc604c7
#define QAM_SET_ARRAY_RVA 0x7f7114
#define QAM_ROWS 4
#define QAM_VALUE_STATE 0x18
#define QAM_IN_HAND 3
#define QAM_OCCUPIED 2
#define QAM_CVAR "m4_qam_ammo"
#define QAM_CVAR_START "-1 -1"

static void *(*qam_set_array_original)(void *, void *, void *, void *);
static void *qam_cvar;
static int qam_tried;
static char qam_text[32] = QAM_CVAR_START, qam_written[32] = QAM_CVAR_START;
static volatile LONG qam_lock;
static char qam_line[96];

int m4_qam_rows(unsigned char *array, char *text, size_t size) {
    unsigned char **begin = *(unsigned char ***)array, **end = *(unsigned char ***)(array + 8);
    if (end - begin != QAM_ROWS * 4) return 0;
    int loaded = 0, in_hand = -1, states[QAM_ROWS * 2];
    for (int row = 0; row < QAM_ROWS; row++) {
        states[row * 2] = *(int *)(begin[row * 4] + QAM_VALUE_STATE);
        states[row * 2 + 1] = *(int *)(begin[row * 4 + 2] + QAM_VALUE_STATE);
        if (states[row * 2] == QAM_IN_HAND) in_hand = row;
        if (states[row * 2 + 1] >= QAM_OCCUPIED) loaded |= 1 << row;
    }
    snprintf(text, size, "%d %d", loaded, in_hand);
    char line[96];
    snprintf(line, sizeof(line), "qam: slot states %d %d, %d %d, %d %d, %d %d gives \"%s\"",
             states[0], states[1], states[2], states[3], states[4], states[5], states[6], states[7], text);
    if (strcmp(line, qam_line)) {
        memcpy(qam_line, line, sizeof(line));
        note(line);
    }
    return 1;
}

static void *qam_set_array(void *element, void *name, void *array, void *extra) {
    char text[sizeof(qam_text)];
    if (array && m4_qam_rows(array, text, sizeof(text))) {
        while (InterlockedCompareExchange(&qam_lock, 1, 0)) YieldProcessor();
        memcpy(qam_text, text, sizeof(text));
        InterlockedExchange(&qam_lock, 0);
    }
    return qam_set_array_original(element, name, array, extra);
}

void m4_qam_console(void *console) {
    if (!qam_cvar) {
        if (qam_tried) return;
        qam_tried = 1;
        qam_cvar = m4_string_cvar(console, QAM_CVAR, QAM_CVAR_START, "M4 weapon bar: rows with off-hand ammo (bit mask) and the row in hand, set by m4_autofire.asi");
        if (!qam_cvar) return;
    }
    char text[sizeof(qam_text)];
    while (InterlockedCompareExchange(&qam_lock, 1, 0)) YieldProcessor();
    memcpy(text, qam_text, sizeof(text));
    InterlockedExchange(&qam_lock, 0);
    if (!strcmp(text, qam_written)) return;
    m4_cvar_set(qam_cvar, text);
    memcpy(qam_written, text, sizeof(text));
}

static int qam_install(unsigned char *game) {
    unsigned char *call = game + QAM_SET_ARRAY_CALL_RVA;
    if (call[0] != 0xe8 || call + 5 + *(int *)(call + 1) != game + QAM_SET_ARRAY_RVA) {
        note("qam: signature mismatch; no weapon bar hook installed");
        return 0;
    }
    unsigned char *entry = near_page(game);
    if (!entry) {
        note("qam: no nearby page; no weapon bar hook installed");
        return 0;
    }
    qam_set_array_original = (void *)(game + QAM_SET_ARRAY_RVA);
    emit_jump(entry, (void *)qam_set_array);
    FlushInstructionCache(GetCurrentProcess(), entry, 14);
    put_call(call, entry, 5);
    note("qam: weapon bar hook installed");
    return 1;
}

DWORD WINAPI qam_hook(LPVOID unused) {
    (void)unused;
    HMODULE game;
    while (!(game = GetModuleHandleA("WHGame.dll"))) Sleep(1);
    qam_install((unsigned char *)game);
    return 0;
}
