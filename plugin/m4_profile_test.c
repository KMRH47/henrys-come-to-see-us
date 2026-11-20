#include "m4_profile.c"

#define require(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

volatile LONG tag_armed = 1;
static double since_shot = 1e12;
static int aim_held, mag_empty;
static char noted[1 << 16];
static char cvar_text[16];
static int cvar_made;

void note(const char *text) {
    size_t used = strlen(noted);
    snprintf(noted + used, sizeof(noted) - used, "%s\n", text);
}

double m4_since_shot_ms(void) { return since_shot; }
int m4_aim_held(void) { return aim_held; }
int m4_mag_empty(void) { return mag_empty; }

void *m4_string_cvar(void *console, const char *name, const char *start, const char *help) {
    (void)console, (void)name, (void)help;
    cvar_made++;
    snprintf(cvar_text, sizeof(cvar_text), "%s", start);
    return cvar_text;
}

void m4_cvar_set(void *cvar, const char *text) { snprintf(cvar, sizeof(cvar_text), "%s", text); }

static volatile LONG reached, release;
static NT_TIB *worker_stack;
static HANDLE gate;

__attribute__((noinline)) static int inner(volatile int *sink) {
    volatile char local[700];
    local[0] = (char)*sink;
    InterlockedExchange(&reached, 1);
    while (!release) YieldProcessor();
    return local[0] + *sink;
}

__attribute__((noinline)) static int middle(volatile int *sink, int size) {
    volatile char *room = __builtin_alloca(size);
    room[0] = 3;
    return inner(sink) + room[0];
}

__attribute__((noinline)) static int outer(volatile int *sink) {
    volatile int kept[40];
    kept[0] = middle(sink, 64 + *sink) + 1;
    return kept[0] + *sink;
}

__attribute__((noinline)) static int inner_wait(volatile int *sink) {
    volatile char local[300];
    local[0] = (char)*sink;
    InterlockedExchange(&reached, 2);
    WaitForSingleObject(gate, INFINITE);
    return local[0] + *sink;
}

__attribute__((noinline)) static int outer_wait(volatile int *sink) {
    volatile int kept[12];
    kept[0] = inner_wait(sink) + 1;
    return kept[0] + *sink;
}

static DWORD WINAPI worker(LPVOID unused) {
    (void)unused;
    volatile int sink = 5;
    worker_stack = (NT_TIB *)NtCurrentTeb();
    sink += outer(&sink);
    sink += outer_wait(&sink);
    return (DWORD)sink;
}

static int index_of(const ULONG64 *chain, int depth, void *function) {
    for (int i = 0; i < depth; i++)
        if (chain[i] == (ULONG64)function) return i;
    return -1;
}

int main(void) {
    static ULONG64 chain[DEPTH];
    CONTEXT context;
    gate = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    require(thread);
    while (reached != 1) Sleep(1);
    profiled = thread;
    profiled_stack = worker_stack;
    modules_read();
    require(module_count > 1);
    require(module_of((ULONG64)inner) && module_of((ULONG64)inner)->count > 0);
    for (int round = 0; round < 50; round++) {
        require(capture(&context));
        int depth = walk(&context, chain);
        int a = index_of(chain, depth, inner), b = index_of(chain, depth, middle), c = index_of(chain, depth, outer), d = index_of(chain, depth, worker);
        require(a == 0 && b == 1 && c == 2 && d == 3);
        require(depth > 4 && depth < DEPTH);
    }
    puts("PASS: a running thread unwinds through a frame-pointer function to its start routine and beyond");

    InterlockedExchange(&release, 1);
    while (reached != 2) Sleep(1);
    int waiting = 0;
    for (int tries = 0; tries < 2000 && !waiting; tries++) {
        require(capture(&context));
        int depth = walk(&context, chain);
        int a = index_of(chain, depth, inner_wait), b = index_of(chain, depth, outer_wait), c = index_of(chain, depth, worker);
        waiting = a > 0 && b == a + 1 && c == b + 1 && module_of(chain[0]) != module_of((ULONG64)inner);
        if (!waiting) Sleep(1);
    }
    require(waiting);
    char name[200];
    name_of(chain[0], name, sizeof(name));
    require(strchr(name, '!') || strchr(name, '+'));
    printf("PASS: a waiting thread unwinds from %s back into its callers\n", name);
    SetEvent(gate);
    WaitForSingleObject(thread, 5000);

    profiled = NULL;
    m4_profile = 1;
    m4_profile_mark("FrameTick");
    require(marks.depth == 1 && !strcmp(steps[marks.steps[0]].name, "FrameTick") && profiled && profiled_stack == (NT_TIB *)NtCurrentTeb());
    m4_profile_mark("PoseTick");
    m4_profile_mark("SweepSpent");
    mark_row deep = marks;
    require(deep.depth == 3);
    m4_profile_mark("");
    m4_profile_mark("");
    m4_profile_mark("HoldTick");
    mark_row hold = marks;
    m4_profile_mark("");
    mark_row tick = marks;
    m4_profile_mark("");
    m4_profile_mark("");
    require(marks.depth == 0);
    mark_row out = marks;
    for (int i = 0; i < 40; i++) m4_profile_mark("Deep");
    require(marks.depth == 40 && marked(&marks) == MARK_DEPTH);
    for (int i = 0; i < 40; i++) m4_profile_mark("");
    require(marks.depth == 0);
    m4_profile_mark("HoldTick");
    m4_profile_mark("FrameTick");
    require(marks.depth == 1 && step_count == 5);
    m4_profile_mark("");
    m4_profile = 0;
    m4_profile_mark("Ignored");
    require(marks.depth == 0 && step_count == 5);
    puts("PASS: step marks nest, unwind, survive a deep stack and start over at each frame tick");

    ULONG64 hot[] = {(ULONG64)inner, (ULONG64)middle, (ULONG64)outer}, cold[] = {(ULONG64)outer_wait, (ULONG64)outer};
    for (int i = 0; i < 50; i++) count_sample(FIRE, &deep, hot, 3);
    for (int i = 0; i < 10; i++) count_sample(FIRE, &hold, cold, 2);
    for (int i = 0; i < 20; i++) count_sample(FIRE, &tick, cold, 2);
    for (int i = 0; i < 20; i++) count_sample(FIRE, &out, hot, 3);
    for (int i = 0; i < 10; i++) count_sample(AIM, &hold, cold, 2);
    for (int i = 0; i < 90; i++) count_sample(AIM, &out, cold, 2);
    frame_sums[FIRE].frames = 10, frame_sums[FIRE].ms = 200;
    frame_sums[AIM].frames = 10, frame_sums[AIM].ms = 80;
    require(samples[FIRE] == 100 && in_mod[FIRE] == 80 && in_mod[AIM] == 10);
    require(slot_of((ULONG64)inner)->counts[FIRE][SELF] == 50 && slot_of((ULONG64)outer)->counts[FIRE][UNDER] == 80);
    noted[0] = 0;
    report();
    require(strstr(noted, "profile: report 1 | back 0 samples, 0.00 ms a frame over 0 frames, the mod 0.000 ms | hand 0 samples"));
    require(strstr(noted, "| aim 100 samples, 8.00 ms a frame over 10 frames, the mod 0.800 ms | fire 100 samples, 20.00 ms a frame over 10 frames, the mod 16.000 ms |"));
    require(strstr(noted, "profile: fire step 16.000 ms a frame, 4.000 of it its own | FrameTick"));
    require(strstr(noted, "profile: fire step 10.000 ms a frame, 0.000 of it its own | PoseTick"));
    require(strstr(noted, "profile: fire step 10.000 ms a frame, 10.000 of it its own | SweepSpent"));
    require(strstr(noted, "profile: fire step 2.000 ms a frame, 2.000 of it its own | HoldTick"));
    require(strstr(noted, "profile: aim step 0.800 ms a frame, 0.800 of it its own | HoldTick"));
    require(strstr(noted, "profile: fire step") < strstr(noted, "profile: fire call") && !strstr(noted, "profile: back step") && !strstr(noted, "Deep"));
    char want[260];
    name_of((ULONG64)inner, name, sizeof(name));
    snprintf(want, sizeof(want), "profile: fire call 10.000 ms a frame in it, 10.000 under it | %s", name);
    require(strstr(noted, want));
    name_of((ULONG64)outer, name, sizeof(name));
    snprintf(want, sizeof(want), "profile: fire call 0.000 ms a frame in it, 16.000 under it | %s", name);
    require(!strstr(noted, want));
    name_of((ULONG64)outer_wait, name, sizeof(name));
    snprintf(want, sizeof(want), "profile: fire call 6.000 ms a frame in it, 6.000 under it | %s", name);
    require(strstr(noted, want));
    noted[0] = 0;
    stack_note(FIRE, &deep, hot, 3);
    require(strstr(noted, "profile: fire stack > FrameTick > PoseTick > SweepSpent | "));
    puts("PASS: the report charges each sample to the Lua steps it ran under and names the game calls inside the mod");

    ULONG64 twice[] = {(ULONG64)inner, (ULONG64)middle, (ULONG64)inner};
    mark_row again = {.depth = 3, .steps = {deep.steps[1], deep.steps[2], deep.steps[1]}};
    count_sample(LOAD, &again, twice, 3);
    require(slot_of((ULONG64)inner)->counts[LOAD][UNDER] == 1 && steps[deep.steps[1]].counts[LOAD][UNDER] == 1 && steps[deep.steps[1]].counts[LOAD][SELF] == 1);
    puts("PASS: a step or function met twice in one sample counts once");

    m4_profile_console(NULL, "m4_other");
    require(!cvar_made);
    m4_profile_console(NULL, PROFILE_CVAR);
    require(cvar_made == 1 && !strcmp(cvar_text, "0"));
    m4_profile = 1;
    m4_profile_console(NULL, PROFILE_CVAR);
    require(cvar_made == 1 && !strcmp(cvar_text, "1"));
    m4_profile = 0;
    m4_profile_console(NULL, PROFILE_CVAR);
    require(!strcmp(cvar_text, "0"));
    puts("PASS: m4_profile reads 1 only while the setting is on");

    static const struct { double since; int aim, empty, armed, kind; } states[] = {
        {100, 1, 0, 1, FIRE}, {800, 1, 0, 1, -1}, {2000, 1, 0, 1, AIM}, {2000, 0, 0, 1, HAND}, {2000, 1, 1, 1, LOAD},
        {100, 1, 1, 1, FIRE}, {7000, 0, 1, 1, -1}, {100, 1, 0, 0, BACK}, {1e12, 0, 0, 0, BACK}
    };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        since_shot = states[i].since, aim_held = states[i].aim, mag_empty = states[i].empty, tag_armed = states[i].armed;
        require(kind_now() == states[i].kind);
    }
    puts("PASS: a frame counts as back, hand, aim, fire or load by the gun's state, and as nothing while a burst settles");
    return 0;
}
