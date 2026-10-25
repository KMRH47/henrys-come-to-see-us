#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

void note(const char *text);
unsigned char *near_page(unsigned char *base);
unsigned char *emit(unsigned char *at, const void *bytes, size_t length);
unsigned char *emit_jump(unsigned char *at, void *target);
void put_call(unsigned char *from, unsigned char *to, size_t length);

#define EVENTS_LOADER_RVA 0xae73f4
static const unsigned char EVENTS_LOADER_BYTES[] = {
    0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20, 0x55,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0xa8, 0x08, 0xfe, 0xff, 0xff, 0x48
};
#define EVENTS_CALL_RVA 0x20f4a1d
#define EVENTS_CALL_OFFSET 12
static const unsigned char EVENTS_CALL_BYTES[] = {
    0x49, 0x8b, 0x07, 0x49, 0x8b, 0xcf, 0xff, 0x50, 0x60, 0x48, 0x8b, 0xd0, 0xe8, 0xc6, 0x29, 0x9f,
    0xfe
};
#define EVENTS_NODE_RVA 0xae7508
#define EVENTS_NODE_OFFSET 14
static const unsigned char EVENTS_NODE_BYTES[] = {
    0x48, 0x8b, 0x01, 0xff, 0x90, 0x60, 0x01, 0x00, 0x00, 0x48, 0x8b, 0x4c, 0x24, 0x30, 0x44, 0x8b,
    0xf0, 0x85, 0xc0, 0x0f, 0x84, 0x7c, 0x01, 0x00, 0x00, 0x48, 0x8b, 0x01, 0x48, 0x8d, 0x15, 0xc5,
    0xb2, 0xf6, 0x02, 0xff, 0x90, 0x08, 0x01, 0x00, 0x00, 0x48, 0x8b, 0xf0, 0xb9, 0x18, 0x00
};
#define EVENTS_NODE_ATTR_SLOT 33
#define EVENTS_MALE_PATH "humans/male/male.animevents"
#define EVENTS_MOD_PATH "animations/humans/male/m4_male.animevents"
#define EVENTS_CLAIM_LIMIT 1024
#define EVENTS_LOGGED_LOADS 3

static const unsigned char EVENTS_NODE_STUB_HEAD[] = {
    0x51, 0x48, 0x83, 0xec, 0x20, 0x89, 0xc2, 0x48, 0xb8
};
static const unsigned char EVENTS_NODE_STUB_TAIL[] = {
    0xff, 0xd0, 0x48, 0x83, 0xc4, 0x20, 0x59, 0x41, 0x89, 0xc6, 0x85, 0xc0, 0xc3
};

static const struct { size_t rva; const unsigned char *bytes; size_t length; } events_signatures[] = {
    {EVENTS_LOADER_RVA, EVENTS_LOADER_BYTES, sizeof(EVENTS_LOADER_BYTES)},
    {EVENTS_CALL_RVA, EVENTS_CALL_BYTES, sizeof(EVENTS_CALL_BYTES)},
    {EVENTS_NODE_RVA, EVENTS_NODE_BYTES, sizeof(EVENTS_NODE_BYTES)},
};

static unsigned char (*events_original)(void *, const char *);
static CRITICAL_SECTION events_lock;
static volatile LONG events_phase;
static volatile DWORD events_owner;
static unsigned long long events_claims[EVENTS_CLAIM_LIMIT];
static unsigned events_claimed;
static unsigned events_dropped;
static LONG events_loads;

static char events_fold(char c) {
    if (c == '\\') return '/';
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

static unsigned long long events_hash(const char *name) {
    unsigned long long hash = 1469598103934665603ull;
    for (; *name; name++) hash = (hash ^ (unsigned char)events_fold(*name)) * 1099511628211ull;
    return hash;
}

static int events_is_male(const char *path) {
    size_t length = path ? strlen(path) : 0, tail = sizeof(EVENTS_MALE_PATH) - 1;
    if (length < tail) return 0;
    for (size_t i = 0; i < tail; i++)
        if (events_fold(path[length - tail + i]) != EVENTS_MALE_PATH[i]) return 0;
    return 1;
}

static int events_claimed_name(unsigned long long hash) {
    for (unsigned i = 0; i < events_claimed; i++)
        if (events_claims[i] == hash) return 1;
    return 0;
}

static void events_claim(unsigned long long hash) {
    if (events_claimed_name(hash)) return;
    if (events_claimed == EVENTS_CLAIM_LIMIT) {
        events_dropped++;
        return;
    }
    events_claims[events_claimed++] = hash;
}

static int events_node(unsigned char *node, int count) {
    LONG phase = events_phase;
    if (!phase || GetCurrentThreadId() != events_owner || !node) return count;
    const char *(*attribute)(void *, const char *) = (*(const char *(***)(void *, const char *))node)[EVENTS_NODE_ATTR_SLOT];
    const char *name = attribute(node, "name");
    if (!name) return count;
    unsigned long long hash = events_hash(name);
    if (phase == 1) {
        events_claim(hash);
        return count;
    }
    return events_claimed_name(hash) ? 0 : count;
}

static unsigned char events_load(void *skeleton, const char *path) {
    if (!events_is_male(path)) return events_original(skeleton, path);
    EnterCriticalSection(&events_lock);
    events_owner = GetCurrentThreadId();
    events_claimed = 0;
    events_dropped = 0;
    events_phase = 1;
    unsigned char mod = events_original(skeleton, EVENTS_MOD_PATH);
    events_phase = 2;
    unsigned char result = events_original(skeleton, path);
    events_phase = 0;
    events_owner = 0;
    unsigned claimed = events_claimed, dropped = events_dropped;
    LeaveCriticalSection(&events_lock);
    if (InterlockedIncrement(&events_loads) <= EVENTS_LOGGED_LOADS) {
        char line[200];
        snprintf(line, sizeof(line), "events: male events loaded, %s %s, %u animations claimed%s", EVENTS_MOD_PATH,
                 mod ? "read" : "missing", claimed, dropped ? " (claim table full)" : "");
        note(line);
    }
    return result;
}

static unsigned char *events_emit_node_stub(unsigned char *at, void *target) {
    at = emit(at, EVENTS_NODE_STUB_HEAD, sizeof(EVENTS_NODE_STUB_HEAD));
    at = emit(at, &target, sizeof(target));
    return emit(at, EVENTS_NODE_STUB_TAIL, sizeof(EVENTS_NODE_STUB_TAIL));
}

static int events_install(unsigned char *game) {
    for (size_t i = 0; i < sizeof(events_signatures) / sizeof(events_signatures[0]); i++) {
        if (memcmp(game + events_signatures[i].rva, events_signatures[i].bytes, events_signatures[i].length)) {
            note("events: signature mismatch; no events hooks installed");
            return 0;
        }
    }
    unsigned char *entry = near_page(game);
    if (!entry) {
        note("events: no nearby page; no events hooks installed");
        return 0;
    }
    InitializeCriticalSection(&events_lock);
    events_original = (void *)(game + EVENTS_LOADER_RVA);
    unsigned char *stub = emit_jump(entry, (void *)events_load);
    stub = (unsigned char *)(((ULONG_PTR)stub + 15) & ~(ULONG_PTR)15);
    events_emit_node_stub(stub, (void *)events_node);
    FlushInstructionCache(GetCurrentProcess(), entry, (size_t)(stub - entry) + 64);
    put_call(game + EVENTS_NODE_RVA + EVENTS_NODE_OFFSET, stub, 5);
    put_call(game + EVENTS_CALL_RVA + EVENTS_CALL_OFFSET, entry, 5);
    note("events: male events merge hooks installed");
    return 1;
}

DWORD WINAPI events_hook(LPVOID unused) {
    (void)unused;
    HMODULE game;
    while (!(game = GetModuleHandleA("WHGame.dll"))) Sleep(1);
    events_install((unsigned char *)game);
    return 0;
}
