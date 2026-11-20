#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PSAPI_VERSION 2
#include <psapi.h>

void note(const char *text);
double m4_since_shot_ms(void);
int m4_aim_held(void);
int m4_mag_empty(void);
void *m4_string_cvar(void *console, const char *name, const char *start, const char *help);
void m4_cvar_set(void *cvar, const char *text);
extern volatile LONG tag_armed;

#define PROFILE_CVAR "m4_profile"
#define TICK_STEP "FrameTick"
#define SAMPLE_MS 4
#define REPORT_MS 30000
#define FIRE_MS 250
#define REST_MS 1500
#define LOAD_MS 6000
#define FRAME_FRESH_MS 200
#define STACK_BYTES 0x40000
#define DEPTH 64
#define MODULES 512
#define MODULES_EVERY 500
#define SLOTS 16384
#define STEPS 256
#define STEP_NAME 40
#define MARK_DEPTH 16
#define STEP_ROWS 24
#define CALL_ROWS 12
#define STACK_EVERY 25
#define EXPORT_REACH 0x2000
#define UNWIND_CHAINED 4
#define UNWIND_PUSH 0
#define UNWIND_ALLOC_LARGE 1
#define UNWIND_ALLOC_SMALL 2
#define UNWIND_SET_FRAME 3
#define UNWIND_SAVE 4
#define UNWIND_SAVE_FAR 5
#define UNWIND_MACHINE_FRAME 10
#define RSP 4
enum { BACK, HAND, AIM, FIRE, LOAD, KINDS };
static const char *const kind_names[KINDS] = {"back", "hand", "aim", "fire", "load"};
enum { SELF, UNDER };

volatile LONG m4_profile;

typedef struct { ULONG64 base, end; const RUNTIME_FUNCTION *functions; DWORD count; char name[48]; } module_row;
typedef struct { ULONG64 rip, reg[16]; } frame_state;
typedef struct { ULONG64 key; unsigned counts[KINDS][2]; } slot_row;
typedef struct { LONG depth; unsigned char steps[MARK_DEPTH]; } mark_row;

static module_row modules[MODULES];
static int module_count;
static struct { ULONG64 low; size_t size; unsigned char bytes[STACK_BYTES]; } copied;
static slot_row slots[SLOTS];
static struct { char name[STEP_NAME]; unsigned counts[KINDS][2]; } steps[STEPS];
static int step_count;
static mark_row marks, seen;
static unsigned samples[KINDS], in_mod[KINDS];
static struct { unsigned frames; double ms; } frame_sums[KINDS];
static HANDLE profiled, wake;
static NT_TIB *profiled_stack;
static LARGE_INTEGER profile_frequency;
static volatile LONG64 frame_at;

static void modules_read(void) {
    static HMODULE handles[MODULES];
    DWORD bytes = 0;
    HANDLE process = GetCurrentProcess();
    if (!K32EnumProcessModules(process, handles, sizeof(handles), &bytes)) return;
    DWORD found = bytes / sizeof(HMODULE) < MODULES ? bytes / sizeof(HMODULE) : MODULES;
    int count = 0;
    for (DWORD i = 0; i < found; i++) {
        MODULEINFO info;
        if (!K32GetModuleInformation(process, handles[i], &info, sizeof(info)) || info.SizeOfImage < 0x1000) continue;
        const unsigned char *base = info.lpBaseOfDll;
        const IMAGE_NT_HEADERS *nt = (const void *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
        IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        module_row *row = &modules[count++];
        row->base = (ULONG64)base;
        row->end = row->base + info.SizeOfImage;
        int listed = dir.VirtualAddress && (ULONG64)dir.VirtualAddress + dir.Size <= info.SizeOfImage;
        row->functions = listed ? (const void *)(base + dir.VirtualAddress) : NULL;
        row->count = listed ? dir.Size / sizeof(RUNTIME_FUNCTION) : 0;
        if (!K32GetModuleBaseNameA(process, handles[i], row->name, sizeof(row->name))) snprintf(row->name, sizeof(row->name), "%p", (void *)base);
    }
    module_count = count;
}

static const module_row *module_of(ULONG64 at) {
    for (int i = 0; i < module_count; i++)
        if (at >= modules[i].base && at < modules[i].end) return &modules[i];
    return NULL;
}

static const void *image(const module_row *module, ULONG64 rva, size_t size) {
    return rva + size <= module->end - module->base ? (const void *)(module->base + rva) : NULL;
}

static const RUNTIME_FUNCTION *function_of(const module_row *module, DWORD rva) {
    DWORD low = 0, high = module->count;
    while (low < high) {
        DWORD middle = low + (high - low) / 2;
        const RUNTIME_FUNCTION *function = &module->functions[middle];
        if (rva < function->BeginAddress) high = middle;
        else if (rva >= function->EndAddress) low = middle + 1;
        else return function;
    }
    return NULL;
}

static int stack_read(ULONG64 at, ULONG64 *value) {
    if (at < copied.low || at + 8 > copied.low + copied.size) return 0;
    memcpy(value, copied.bytes + (at - copied.low), 8);
    return 1;
}

static unsigned code_slots(unsigned op, unsigned detail) {
    static const unsigned char sizes[] = {1, 2, 1, 1, 2, 3, 1, 2, 2, 3, 1};
    if (op == UNWIND_ALLOC_LARGE) return detail ? 3 : 2;
    return op < sizeof(sizes) ? sizes[op] : 1;
}

static int frame_set_by(const unsigned char *codes, unsigned count, DWORD offset) {
    for (unsigned i = 0; i < count; i += code_slots(codes[i * 2 + 1] & 15, codes[i * 2 + 1] >> 4))
        if ((codes[i * 2 + 1] & 15) == UNWIND_SET_FRAME) return offset >= codes[i * 2];
    return 0;
}

static int step_back(const module_row *module, const RUNTIME_FUNCTION *function, int entered, frame_state *state) {
    ULONG64 rsp = state->reg[RSP];
    DWORD offset = (DWORD)(state->rip - module->base) - function->BeginAddress;
    int machine = 0;
    for (int chain = 0; chain < 8; chain++) {
        if (function->UnwindData & 1) {
            function = image(module, function->UnwindData & ~1u, sizeof(*function));
            if (!function) return 0;
            continue;
        }
        const unsigned char *info = image(module, function->UnwindData, 4);
        if (!info) return 0;
        unsigned version = info[0] & 7, flags = info[0] >> 3, prolog = info[1], count = info[2];
        unsigned frame_register = info[3] & 15, frame_offset = info[3] >> 4;
        if (version != 1 && version != 2) return 0;
        const unsigned char *codes = image(module, (ULONG64)function->UnwindData + 4, ((count + 1) & ~1u) * 2 + sizeof(*function));
        if (!codes) return 0;
        int in_prolog = entered && !chain && offset < prolog;
        int framed = frame_register && (!in_prolog || frame_set_by(codes, count, offset));
        ULONG64 frame = framed ? state->reg[frame_register] - frame_offset * 16 : rsp;
        for (unsigned i = 0; i < count;) {
            unsigned at = codes[i * 2], op = codes[i * 2 + 1] & 15, detail = codes[i * 2 + 1] >> 4, size = code_slots(op, detail);
            if (i + size > count) return 0;
            if (!(in_prolog && offset < at)) {
                USHORT near_value = 0;
                DWORD far_value = 0;
                if (size > 1) memcpy(&near_value, codes + (i + 1) * 2, 2);
                if (size > 2) memcpy(&far_value, codes + (i + 1) * 2, 4);
                if (op == UNWIND_PUSH) {
                    if (!stack_read(rsp, &state->reg[detail])) return 0;
                    rsp += 8;
                } else if (op == UNWIND_ALLOC_LARGE) {
                    rsp += detail ? far_value : (ULONG64)near_value * 8;
                } else if (op == UNWIND_ALLOC_SMALL) {
                    rsp += (detail + 1) * 8;
                } else if (op == UNWIND_SET_FRAME) {
                    rsp = frame;
                } else if (op == UNWIND_SAVE) {
                    if (!stack_read(frame + (ULONG64)near_value * 8, &state->reg[detail])) return 0;
                } else if (op == UNWIND_SAVE_FAR) {
                    if (!stack_read(frame + far_value, &state->reg[detail])) return 0;
                } else if (op == UNWIND_MACHINE_FRAME) {
                    ULONG64 from = rsp + (detail ? 8 : 0), next;
                    if (!stack_read(from, &state->rip) || !stack_read(from + 24, &next)) return 0;
                    rsp = next;
                    machine = 1;
                }
            }
            i += size;
        }
        if (!(flags & UNWIND_CHAINED)) break;
        function = (const RUNTIME_FUNCTION *)(codes + ((count + 1) & ~1u) * 2);
    }
    if (!machine) {
        if (!stack_read(rsp, &state->rip)) return 0;
        rsp += 8;
    }
    state->reg[RSP] = rsp;
    return 1;
}

static int walk(const CONTEXT *context, ULONG64 *chain) {
    frame_state state = {.rip = context->Rip,
                         .reg = {context->Rax, context->Rcx, context->Rdx, context->Rbx, context->Rsp, context->Rbp, context->Rsi, context->Rdi,
                                 context->R8, context->R9, context->R10, context->R11, context->R12, context->R13, context->R14, context->R15}};
    int depth = 0;
    while (depth < DEPTH) {
        ULONG64 returned = state.rip, at = depth ? returned - 1 : returned, was = state.reg[RSP];
        const module_row *module = module_of(at);
        if (!module) {
            if (!depth) chain[depth++] = returned;
            break;
        }
        const RUNTIME_FUNCTION *function = module->count ? function_of(module, (DWORD)(at - module->base)) : NULL;
        chain[depth++] = function ? module->base + function->BeginAddress : returned;
        if (function) {
            state.rip = at;
            if (!step_back(module, function, depth == 1, &state)) break;
        } else {
            if (!stack_read(was, &state.rip)) break;
            state.reg[RSP] = was + 8;
        }
        if (state.reg[RSP] <= was || !state.rip) break;
    }
    return depth;
}

static int capture(CONTEXT *context) {
    int got = 0;
    copied.size = 0;
    if (SuspendThread(profiled) == (DWORD)-1) return 0;
    context->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (GetThreadContext(profiled, context)) {
        ULONG64 low = context->Rsp, limit = (ULONG64)profiled_stack->StackLimit, base = (ULONG64)profiled_stack->StackBase;
        got = 1;
        if (low >= limit && low < base) {
            copied.low = low;
            copied.size = base - low < STACK_BYTES ? (size_t)(base - low) : STACK_BYTES;
            memcpy(copied.bytes, (const void *)low, copied.size);
        }
        seen = marks;
    }
    ResumeThread(profiled);
    return got;
}

static slot_row *slot_of(ULONG64 key) {
    unsigned at = (unsigned)((key * 0x9E3779B97F4A7C15ull) >> 40) & (SLOTS - 1);
    for (unsigned tries = 0; tries < SLOTS; tries++, at = (at + 1) & (SLOTS - 1)) {
        if (slots[at].key == key) return &slots[at];
        if (!slots[at].key) {
            slots[at].key = key;
            return &slots[at];
        }
    }
    return NULL;
}

static int step_of(const char *name) {
    unsigned hash = 2166136261u;
    for (const char *c = name; *c; c++) hash = (hash ^ (unsigned char)*c) * 16777619u;
    for (unsigned tries = 0, at = hash % STEPS; tries < STEPS; tries++, at = (at + 1) % STEPS) {
        if (!steps[at].name[0]) {
            if (step_count >= STEPS - 1) return -1;
            snprintf(steps[at].name, sizeof(steps[at].name), "%s", name);
            step_count++;
            return (int)at;
        }
        if (!strncmp(steps[at].name, name, STEP_NAME - 1)) return (int)at;
    }
    return -1;
}

static int marked(const mark_row *row) {
    return row->depth < MARK_DEPTH ? row->depth : MARK_DEPTH;
}

static void count_sample(int kind, const mark_row *row, const ULONG64 *chain, int depth) {
    int inside = marked(row);
    samples[kind]++;
    if (!inside) return;
    in_mod[kind]++;
    steps[row->steps[inside - 1]].counts[kind][SELF]++;
    for (int i = 0; i < inside; i++) {
        int repeated = 0;
        for (int k = 0; k < i && !repeated; k++) repeated = row->steps[k] == row->steps[i];
        if (!repeated) steps[row->steps[i]].counts[kind][UNDER]++;
    }
    for (int i = 0; i < depth; i++) {
        int repeated = 0;
        for (int k = 0; k < i && !repeated; k++) repeated = chain[k] == chain[i];
        slot_row *slot = slot_of(chain[i]);
        if (!slot) continue;
        if (!i) slot->counts[kind][SELF]++;
        if (!repeated) slot->counts[kind][UNDER]++;
    }
}

static void name_of(ULONG64 at, char *text, size_t size) {
    const module_row *module = module_of(at);
    if (!module) {
        snprintf(text, size, "%p", (void *)at);
        return;
    }
    DWORD rva = (DWORD)(at - module->base), best = 0, image_size = (DWORD)(module->end - module->base);
    const unsigned char *base = (const unsigned char *)module->base;
    const IMAGE_NT_HEADERS *nt = (const void *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    const IMAGE_EXPORT_DIRECTORY *exports = dir.VirtualAddress ? image(module, dir.VirtualAddress, sizeof(*exports)) : NULL;
    const char *name = NULL;
    if (exports) {
        const DWORD *names = image(module, exports->AddressOfNames, (size_t)exports->NumberOfNames * 4);
        const WORD *ordinals = image(module, exports->AddressOfNameOrdinals, (size_t)exports->NumberOfNames * 2);
        const DWORD *functions = image(module, exports->AddressOfFunctions, (size_t)exports->NumberOfFunctions * 4);
        for (DWORD i = 0; names && ordinals && functions && i < exports->NumberOfNames; i++) {
            if (ordinals[i] >= exports->NumberOfFunctions || names[i] >= image_size) continue;
            DWORD start = functions[ordinals[i]];
            if (start <= rva && start >= best && rva - start < EXPORT_REACH) {
                best = start;
                name = (const char *)base + names[i];
            }
        }
    }
    if (name) snprintf(text, size, "%s!%.60s+0x%lx", module->name, name, (unsigned long)(rva - best));
    else snprintf(text, size, "%s+0x%lx", module->name, (unsigned long)rva);
}

static double frame_ms(int kind) {
    return frame_sums[kind].frames ? frame_sums[kind].ms / frame_sums[kind].frames : 0;
}

static double cost(unsigned count, int kind) {
    return samples[kind] ? (double)count / samples[kind] * frame_ms(kind) : 0;
}

static void report_steps(int kind) {
    static unsigned char taken[STEPS];
    char line[200];
    memset(taken, 0, sizeof(taken));
    for (int n = 0; n < STEP_ROWS; n++) {
        int best = -1;
        for (int i = 0; i < STEPS; i++)
            if (steps[i].name[0] && !taken[i] && steps[i].counts[kind][UNDER] && (best < 0 || steps[i].counts[kind][UNDER] > steps[best].counts[kind][UNDER])) best = i;
        if (best < 0) break;
        taken[best] = 1;
        snprintf(line, sizeof(line), "profile: %s step %.3f ms a frame, %.3f of it its own | %s", kind_names[kind],
                 cost(steps[best].counts[kind][UNDER], kind), cost(steps[best].counts[kind][SELF], kind), steps[best].name);
        note(line);
    }
}

static void report_calls(int kind) {
    static unsigned char taken[SLOTS];
    char line[320], name[200];
    memset(taken, 0, sizeof(taken));
    for (int n = 0; n < CALL_ROWS; n++) {
        int best = -1;
        for (int i = 0; i < SLOTS; i++)
            if (slots[i].key && !taken[i] && slots[i].counts[kind][SELF] && (best < 0 || slots[i].counts[kind][SELF] > slots[best].counts[kind][SELF])) best = i;
        if (best < 0) break;
        taken[best] = 1;
        name_of(slots[best].key, name, sizeof(name));
        snprintf(line, sizeof(line), "profile: %s call %.3f ms a frame in it, %.3f under it | %s", kind_names[kind],
                 cost(slots[best].counts[kind][SELF], kind), cost(slots[best].counts[kind][UNDER], kind), name);
        note(line);
    }
}

static void report(void) {
    static int serial;
    char line[480];
    int used = snprintf(line, sizeof(line), "profile: report %d", ++serial);
    for (int kind = 0; kind < KINDS; kind++)
        used += snprintf(line + used, sizeof(line) - used, " | %s %u samples, %.2f ms a frame over %u frames, the mod %.3f ms", kind_names[kind],
                         samples[kind], frame_ms(kind), frame_sums[kind].frames, cost(in_mod[kind], kind));
    note(line);
    for (int kind = 0; kind < KINDS; kind++) {
        if (!in_mod[kind]) continue;
        report_steps(kind);
        report_calls(kind);
    }
}

static void stack_note(int kind, const mark_row *row, const ULONG64 *chain, int depth) {
    static char line[DEPTH * 96 + MARK_DEPTH * STEP_NAME + 96];
    char name[200];
    int used = snprintf(line, sizeof(line), "profile: %s stack", kind_names[kind]);
    for (int i = 0; i < marked(row); i++) used += snprintf(line + used, sizeof(line) - used, " > %s", steps[row->steps[i]].name);
    used += snprintf(line + used, sizeof(line) - used, " |");
    for (int i = 0; i < depth && used < (int)sizeof(line) - 96; i++) {
        name_of(chain[i], name, sizeof(name));
        used += snprintf(line + used, sizeof(line) - used, "%s%.90s", i ? " < " : " ", name);
    }
    note(line);
}

static int kind_now(void) {
    double since = m4_since_shot_ms();
    if (!tag_armed) return BACK;
    if (since < FIRE_MS) return FIRE;
    if (m4_mag_empty()) return since < LOAD_MS ? LOAD : -1;
    if (since <= REST_MS) return -1;
    return m4_aim_held() ? AIM : HAND;
}

static double since_frame_ms(void) {
    LARGE_INTEGER now;
    LONG64 at = InterlockedCompareExchange64(&frame_at, 0, 0);
    QueryPerformanceCounter(&now);
    return at && profile_frequency.QuadPart ? (double)(now.QuadPart - at) * 1000 / profile_frequency.QuadPart : 1e12;
}

static void frame_mark(void) {
    if (!profiled) {
        QueryPerformanceFrequency(&profile_frequency);
        profiled_stack = (NT_TIB *)NtCurrentTeb();
        profiled = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, GetCurrentThreadId());
        note(profiled ? "profile: sampling the thread that runs the mod's Lua" : "profile: the Lua thread did not open; nothing is sampled");
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    LONG64 last = InterlockedExchange64(&frame_at, now.QuadPart);
    double ms = last ? (double)(now.QuadPart - last) * 1000 / profile_frequency.QuadPart : 0;
    int kind = kind_now();
    if (kind >= 0 && ms > 0 && ms < FRAME_FRESH_MS) {
        frame_sums[kind].frames++;
        frame_sums[kind].ms += ms;
    }
    if (wake) SetEvent(wake);
}

void m4_profile_mark(const char *step) {
    if (!m4_profile) return;
    if (!*step) {
        if (marks.depth > 0) marks.depth--;
        return;
    }
    int tick = !strcmp(step, TICK_STEP), id = step_of(step);
    if (tick) {
        marks.depth = 0;
        frame_mark();
    }
    LONG at = marks.depth;
    if (at < MARK_DEPTH) marks.steps[at] = id >= 0 ? (unsigned char)id : at ? marks.steps[at - 1] : 0;
    marks.depth = at + 1;
}

void m4_profile_console(void *console, const char *name) {
    static void *cvar;
    static int tried;
    static LONG written;
    if (!name || strcmp(name, PROFILE_CVAR)) return;
    if (!cvar) {
        if (tried) return;
        tried = 1;
        cvar = m4_string_cvar(console, PROFILE_CVAR, "0", "M4 profiling: 1 while m4_autofire.asi samples the main thread, set by profile=1 in m4_autofire.ini");
        if (!cvar) return;
    }
    LONG on = m4_profile != 0;
    if (on == written) return;
    m4_cvar_set(cvar, on ? "1" : "0");
    written = on;
}

DWORD WINAPI profile_hook(LPVOID unused) {
    (void)unused;
    static ULONG64 chain[DEPTH];
    HANDLE timer = CreateWaitableTimerW(NULL, FALSE, NULL);
    int unreported = 0, since_modules = MODULES_EVERY;
    wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    for (;;) {
        if (!m4_profile || !profiled || since_frame_ms() > FRAME_FRESH_MS) {
            if (unreported) report();
            unreported = 0;
            WaitForSingleObject(wake, INFINITE);
            continue;
        }
        int kind = kind_now();
        CONTEXT context;
        if (kind >= 0 && capture(&context)) {
            if (++since_modules > MODULES_EVERY) {
                modules_read();
                since_modules = 0;
            }
            int depth = walk(&context, chain);
            count_sample(kind, &seen, chain, depth);
            if (marked(&seen) && in_mod[kind] % STACK_EVERY == 1) stack_note(kind, &seen, chain, depth);
            if (++unreported >= REPORT_MS / SAMPLE_MS) {
                report();
                unreported = 0;
            }
        }
        LARGE_INTEGER wait;
        wait.QuadPart = -(LONGLONG)SAMPLE_MS * 10000;
        if (!timer || !SetWaitableTimer(timer, &wait, 0, NULL, NULL, FALSE) || WaitForSingleObject(timer, INFINITE) != WAIT_OBJECT_0)
            WaitForSingleObject(wake, SAMPLE_MS);
    }
    return 0;
}
