#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#define PSAPI_VERSION 2
#include <psapi.h>

void note(const char *text);

#define LEAD_MS 150
#define PENDING 16
#define BURST_BREAK_MS 200
#define SHOT_BUS "bus:/dieg/combat/combat_oneshot"
#define ONE_SHOTS 16
// The handgonne's shot sends -15 dB into this return; the game's indoor snapshots shape it, outdoors mute it.
#define REVERB_BUS "bus:/RVB_GRP/RVB"
#define REVERB_SEND 0.178f
#define DSP_SEND 24
#define DSP_RETURN 25
// Indoors the game's room reverb carries the tail, so the shot's baked outdoor echo fades out after its crack.
#define DSP_SFXREVERB 19
#define REVERB_WET 11
#define REVERB_DECAY 0
#define INDOOR_WET_DB -40.0f
#define CRACK_MS 250
#define ECHO_FADE_MS 600

typedef struct {
    int cbsize;
    unsigned length, fileoffset;
    int numchannels, defaultfrequency, format;
    unsigned decodebuffersize;
    int initialsubsound, numsubsounds;
    int *inclusionlist;
    int inclusionlistnum;
    void *pcmreadcallback, *pcmsetposcallback, *nonblockcallback;
    const char *dlsname, *encryptionkey;
    int maxpolyphony;
    void *userdata;
    int suggestedsoundtype;
    void *fileuseropen, *fileuserclose, *fileuserread, *fileuserseek, *fileuserasyncread, *fileuserasynccancel;
    void *fileuserdata;
    int filebuffersize, channelorder;
    void *initialsoundgroup;
    unsigned initialseekposition;
    int initialseekpostype, ignoresetfilesystem;
    unsigned audioqueuepolicy, minmidigranularity;
    int nonblockthreadid;
    void *fsbguid;
} SoundInfo;

typedef int(__stdcall *Update)(void *);
typedef int(__stdcall *CoreSystem)(void *, void **);
typedef int(__stdcall *GetBus)(void *, const char *, void **);
typedef int(__stdcall *BusCall)(void *);
typedef int(__stdcall *BusGroup)(void *, void **);
typedef int(__stdcall *CreateSound)(void *, const char *, unsigned, SoundInfo *, void **);
typedef int(__stdcall *PlaySound_)(void *, void *, void *, int, void **);
typedef int(__stdcall *GroupClock)(void *, unsigned long long *, unsigned long long *);
typedef int(__stdcall *SetDelay)(void *, unsigned long long, unsigned long long, int);
typedef int(__stdcall *SetPaused)(void *, int);
typedef int(__stdcall *SetVolume)(void *, float);
typedef int(__stdcall *Stop)(void *);
typedef int(__stdcall *SoftwareFormat)(void *, int *, int *, int *);
typedef int(__stdcall *CreateGroup)(void *, const char *, void **);
typedef int(__stdcall *AddGroup)(void *, void *, int, void **);
typedef int(__stdcall *DspCount)(void *, int *);
typedef int(__stdcall *DspAt)(void *, int, void **);
typedef int(__stdcall *DspType)(void *, int *);
typedef int(__stdcall *DspGetInt)(void *, int, int *, char *, int);
typedef int(__stdcall *CreateDsp)(void *, int, void **);
typedef int(__stdcall *DspSetInt)(void *, int, int);
typedef int(__stdcall *DspSetFloat)(void *, int, float);
typedef int(__stdcall *AddDsp)(void *, int, void *);
typedef int(__stdcall *DspGetFloat)(void *, int, float *, char *, int);
typedef int(__stdcall *AddFade)(void *, unsigned long long, float);

static struct {
    CoreSystem core_system;
    GetBus get_bus;
    BusCall lock;
    BusGroup group_of;
    CreateSound load;
    PlaySound_ play;
    GroupClock clock;
    SetDelay delay;
    SetPaused paused;
    SetVolume volume;
    Stop stop;
    SoftwareFormat format;
    CreateGroup create_group;
    AddGroup add_group;
    DspCount dsp_count;
    DspAt dsp_at;
    DspType dsp_type;
    DspGetInt dsp_get_int;
    CreateDsp create_dsp;
    DspSetInt dsp_set_int;
    DspSetFloat dsp_set_float;
    AddDsp add_dsp;
    DspGetFloat dsp_get_float;
    AddFade add_fade;
    void *room;
    void *studio, *core, *bus, *group, *sound, *reverb_bus;
    int reverb_routed;
    int rate, failed;
} fmod;
static Update original_c, original_cpp;
static char wav_path[MAX_PATH];
static const char *missing;
static volatile LONG trigger_held, beat_ms = 110, volume = 100;
static volatile LONG64 last_shot_tick;
static unsigned long long next_beat, last_clock, largest_step;
static struct { void *channel; unsigned long long start; int beat; } pending[PENDING];
static int beats, skipped;
static volatile LONG shots_total;
static LONG shots_heard, burst_base;
static const char *volatile one_shot_queue[ONE_SHOTS];
static volatile LONG one_shot_head, one_shot_volumes[ONE_SHOTS];
static LONG one_shot_tail;
static struct { const char *name; void *sound; } one_shot_sounds[ONE_SHOTS];
static int crack_ms = CRACK_MS, echo_fade_ms = ECHO_FADE_MS;
static FILETIME echo_written;

static FARPROC symbol(const char *name) {
    const char *modules[] = {"fmodstudio.dll", "fmod.dll", "fmodstudioL.dll", "fmodL.dll"};
    for (size_t i = 0; i < sizeof(modules) / sizeof(modules[0]); i++) {
        HMODULE module = GetModuleHandleA(modules[i]);
        FARPROC found = module ? GetProcAddress(module, name) : NULL;
        if (found) return found;
    }
    if (!missing) missing = name;
    return NULL;
}

static double since_last_shot(void) {
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return (double)(now.QuadPart - InterlockedCompareExchange64(&last_shot_tick, 0, 0)) * 1000 / frequency.QuadPart;
}

static void fail(const char *what, int result) {
    char line[160];
    snprintf(line, sizeof(line), "clock: %s failed (%d); the game plays the shot", what, result);
    note(line);
    fmod.failed = 1;
}

// Sends the M4's group into the reverb return the handgonne's shot uses, once the bus has its return DSP.
static void route_reverb(void) {
    static int tries;
    void *group = NULL, *dsp = NULL, *send = NULL;
    int count = 0, type = 0, id = -1, result;
    if (fmod.reverb_routed || !fmod.reverb_bus || tries++ > 2000) return;
    if (fmod.group_of(fmod.reverb_bus, &group) || !group || fmod.dsp_count(group, &count) || !count) return;
    for (int i = 0; i < count; i++) {
        if (fmod.dsp_at(group, i, &dsp) || !dsp || fmod.dsp_type(dsp, &type)) continue;
        if (type == DSP_RETURN && id < 0) fmod.dsp_get_int(dsp, 0, &id, NULL, 0);
        if (type == DSP_SFXREVERB && !fmod.room) fmod.room = dsp;
    }
    if (!fmod.room) note("clock: no room reverb on " REVERB_BUS "; the shot keeps its echo indoors");
    fmod.reverb_routed = 1;
    char line[160];
    if (id < 0) {
        snprintf(line, sizeof(line), "clock: %s has no return among %d DSPs; the M4 sounds play dry", REVERB_BUS, count);
    } else if ((result = fmod.create_dsp(fmod.core, DSP_SEND, &send)) || (result = fmod.dsp_set_int(send, 0, id)) ||
               (result = fmod.dsp_set_float(send, 1, REVERB_SEND)) || (result = fmod.add_dsp(fmod.group, 0, send))) {
        snprintf(line, sizeof(line), "clock: reverb send failed (%d); the M4 sounds play dry", result);
    } else {
        snprintf(line, sizeof(line), "clock: M4 sounds send %.3f into %s return %d", REVERB_SEND, REVERB_BUS, id);
    }
    note(line);
}

static int attach(void) {
    int result, speakers = 0, raw = 0;
    if (!fmod.core) {
        if ((result = fmod.core_system(fmod.studio, &fmod.core))) return fail("GetCoreSystem", result), 0;
        if ((result = fmod.format(fmod.core, &fmod.rate, &speakers, &raw)) || fmod.rate <= 0)
            return fail("GetSoftwareFormat", result), 0;
        if ((result = fmod.get_bus(fmod.studio, SHOT_BUS, &fmod.bus))) return fail("GetBus " SHOT_BUS, result), 0;
        if ((result = fmod.lock(fmod.bus))) return fail("LockChannelGroup", result), 0;
        if (fmod.get_bus(fmod.studio, REVERB_BUS, &fmod.reverb_bus) || fmod.lock(fmod.reverb_bus)) {
            fmod.reverb_bus = NULL;
            note("clock: no " REVERB_BUS "; the M4 sounds play dry");
        }
        SoundInfo info = {.cbsize = sizeof(SoundInfo), .ignoresetfilesystem = 1};
        if ((result = fmod.load(fmod.core, wav_path, 0x1 | 0x8 | 0x100, &info, &fmod.sound)))
            return fail("CreateSound", result), 0;
        return 0;
    }
    if (!fmod.group) {
        void *bus_group = NULL, *own = NULL;
        if (fmod.group_of(fmod.bus, &bus_group) || !bus_group) return 0;
        // The M4's own group under the bus carries the reverb send for the shot and the reload sounds alike.
        if ((result = fmod.create_group(fmod.core, "m4_autofire", &own)) || (result = fmod.add_group(bus_group, own, 1, NULL)))
            return fail("CreateChannelGroup", result), 0;
        fmod.group = own;
        char line[MAX_PATH + 96];
        snprintf(line, sizeof(line), "clock: shot sound plays in the game's %s at %d Hz", SHOT_BUS, fmod.rate);
        note(line);
    }
    route_reverb();
    return 1;
}

// m4_echo.txt beside m4_shot.wav ("<crack ms> <fade ms>", written by kcd2-forge game echo) retimes the fade live.
static void echo_times(void) {
    char path[MAX_PATH], line[128];
    const char *slash = strrchr(wav_path, '\\');
    snprintf(path, sizeof(path), "%.*s\\m4_echo.txt", slash ? (int)(slash - wav_path) : 0, wav_path);
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data) || !CompareFileTime(&data.ftLastWriteTime, &echo_written)) return;
    echo_written = data.ftLastWriteTime;
    FILE *file = fopen(path, "r");
    if (!file) return;
    if (fscanf(file, "%d %d", &crack_ms, &echo_fade_ms) == 2) {
        snprintf(line, sizeof(line), "clock: indoor echo kept %d ms, faded over %d ms", crack_ms, echo_fade_ms);
        note(line);
    }
    fclose(file);
}

// Whether the room reverb is open, as the game's indoor snapshots open it; logged each time it changes.
static int indoors(void) {
    static int was = -1;
    float wet = -80.0f, decay = 0.0f;
    if (!fmod.room || fmod.dsp_get_float(fmod.room, REVERB_WET, &wet, NULL, 0)) return 0;
    int now = wet > INDOOR_WET_DB;
    if (now != was) {
        fmod.dsp_get_float(fmod.room, REVERB_DECAY, &decay, NULL, 0);
        char line[128];
        snprintf(line, sizeof(line), "clock: %s, room reverb wet %.1f dB decay %.0f ms", now ? "indoors" : "outdoors", wet, decay);
        note(line);
        was = now;
    }
    return now;
}

static void play_at(unsigned long long start) {
    void *channel = NULL;
    if (fmod.play(fmod.core, fmod.sound, fmod.group, 1, &channel) || !channel) return;
    fmod.delay(channel, start, 0, 0);
    if (indoors()) {
        echo_times();
        unsigned long long crack = start + (unsigned long long)fmod.rate * crack_ms / 1000;
        fmod.add_fade(channel, crack, 1.0f);
        fmod.add_fade(channel, crack + (unsigned long long)fmod.rate * echo_fade_ms / 1000, 0.0f);
    }
    fmod.volume(channel, volume / 100.0f);
    fmod.paused(channel, 0);
    pending[beats % PENDING].channel = channel;
    pending[beats % PENDING].start = start;
    pending[beats % PENDING].beat = beats;
    beats++;
}

static void end_burst(unsigned long long clock, LONG shots) {
    int cancelled = 0, fired = (int)(shots - burst_base);
    for (int i = 0; i < PENDING; i++) {
        if (pending[i].channel && pending[i].start > clock && pending[i].beat >= fired) {
            fmod.stop(pending[i].channel);
            cancelled++;
        }
        pending[i].channel = NULL;
    }
    char line[160];
    snprintf(line, sizeof(line), "clock: burst %d beats for %d shots, %d cancelled, %d late, audio clock step up to %.1f ms",
             beats - cancelled, fired, cancelled, skipped, largest_step * 1000.0 / fmod.rate);
    note(line);
    shots_heard = shots;
    next_beat = 0;
    beats = skipped = 0;
    largest_step = 0;
}

// A reload sound: m4_reload\<name>.wav beside m4_shot.wav, loaded on its first play.
static void play_one_shot(const char *name, long volume_percent) {
    int i;
    for (i = 0; i < ONE_SHOTS && one_shot_sounds[i].name && one_shot_sounds[i].name != name; i++) {}
    if (i == ONE_SHOTS) return;
    if (!one_shot_sounds[i].name) {
        char path[MAX_PATH], line[MAX_PATH + 64];
        const char *slash = strrchr(wav_path, '\\');
        snprintf(path, sizeof(path), "%.*s\\m4_reload\\%s.wav", slash ? (int)(slash - wav_path) : 0, wav_path, name);
        SoundInfo info = {.cbsize = sizeof(SoundInfo), .ignoresetfilesystem = 1};
        int result = fmod.load(fmod.core, path, 0x1 | 0x8 | 0x100, &info, &one_shot_sounds[i].sound);
        one_shot_sounds[i].name = name;
        snprintf(line, sizeof(line), "clock: loaded %s (%d)", path, result);
        note(line);
    }
    void *channel = NULL;
    if (!one_shot_sounds[i].sound || fmod.play(fmod.core, one_shot_sounds[i].sound, fmod.group, 1, &channel) || !channel) return;
    fmod.volume(channel, volume_percent / 100.0f);
    fmod.paused(channel, 0);
}

static void on_update(void *studio) {
    if (fmod.failed) return;
    if (!fmod.studio) fmod.studio = studio;
    if (studio != fmod.studio || !attach()) return;
    for (const char *name; (name = InterlockedExchangePointer((void *volatile *)&one_shot_queue[one_shot_tail % ONE_SHOTS], NULL));
         one_shot_tail++)
        play_one_shot(name, one_shot_volumes[one_shot_tail % ONE_SHOTS]);
    unsigned long long clock = 0, parent = 0;
    if (fmod.clock(fmod.group, &clock, &parent)) return;
    unsigned long long lead = (unsigned long long)fmod.rate * LEAD_MS / 1000;
    unsigned long long step = (unsigned long long)beat_ms * fmod.rate / 1000;
    if (last_clock && clock - last_clock > largest_step) largest_step = clock - last_clock;
    last_clock = clock;
    LONG shots = shots_total;
    if (next_beat && (!trigger_held || since_last_shot() > BURST_BREAK_MS)) {
        end_burst(clock, shots);
        return;
    }
    if (!next_beat) {
        if (shots == shots_heard) return;
        if (since_last_shot() > BURST_BREAK_MS) {
            shots_heard = shots;
            return;
        }
        burst_base = shots_heard;
        next_beat = clock + (unsigned long long)fmod.rate * 30 / 1000;
    }
    while (next_beat < clock) {
        next_beat += step;
        skipped++;
    }
    while (next_beat <= clock + lead) {
        play_at(next_beat);
        next_beat += step;
    }
}

static int __stdcall update_c(void *studio) {
    on_update(studio);
    return original_c(studio);
}

static int __stdcall update_cpp(void *studio) {
    on_update(studio);
    return original_cpp(studio);
}

static int patch_imports(HMODULE module, const char *name, void *hook, Update *original) {
    unsigned char *base = (unsigned char *)module;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    int patched = 0;
    for (IMAGE_IMPORT_DESCRIPTOR *entry = (void *)(base + dir.VirtualAddress); entry->Name; entry++) {
        if (_stricmp((char *)base + entry->Name, "fmodstudio.dll") && _stricmp((char *)base + entry->Name, "fmodstudioL.dll"))
            continue;
        IMAGE_THUNK_DATA *names = (void *)(base + (entry->OriginalFirstThunk ? entry->OriginalFirstThunk : entry->FirstThunk));
        IMAGE_THUNK_DATA *slots = (void *)(base + entry->FirstThunk);
        for (; names->u1.AddressOfData; names++, slots++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *import = (void *)(base + names->u1.AddressOfData);
            if (strcmp((char *)import->Name, name)) continue;
            DWORD old;
            if (!VirtualProtect(&slots->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) continue;
            if (!*original) *original = (Update)slots->u1.Function;
            slots->u1.Function = (ULONGLONG)hook;
            VirtualProtect(&slots->u1.Function, sizeof(void *), old, &old);
            patched++;
        }
    }
    return patched;
}

void m4_clock_ready(const char *wav) {
    static int hooked;
    if (hooked) return;
    hooked = 1;
    snprintf(wav_path, sizeof(wav_path), "%s", wav);
#define FIND(slot, type, name) slot = (type)symbol(name)
    FIND(fmod.core_system, CoreSystem, "FMOD_Studio_System_GetCoreSystem");
    FIND(fmod.get_bus, GetBus, "FMOD_Studio_System_GetBus");
    FIND(fmod.lock, BusCall, "FMOD_Studio_Bus_LockChannelGroup");
    FIND(fmod.group_of, BusGroup, "FMOD_Studio_Bus_GetChannelGroup");
    FIND(fmod.load, CreateSound, "FMOD_System_CreateSound");
    FIND(fmod.play, PlaySound_, "FMOD_System_PlaySound");
    FIND(fmod.clock, GroupClock, "FMOD_ChannelGroup_GetDSPClock");
    FIND(fmod.delay, SetDelay, "FMOD_Channel_SetDelay");
    FIND(fmod.paused, SetPaused, "FMOD_Channel_SetPaused");
    FIND(fmod.volume, SetVolume, "FMOD_Channel_SetVolume");
    FIND(fmod.stop, Stop, "FMOD_Channel_Stop");
    FIND(fmod.format, SoftwareFormat, "FMOD_System_GetSoftwareFormat");
    FIND(fmod.create_group, CreateGroup, "FMOD_System_CreateChannelGroup");
    FIND(fmod.add_group, AddGroup, "FMOD_ChannelGroup_AddGroup");
    FIND(fmod.dsp_count, DspCount, "FMOD_ChannelGroup_GetNumDSPs");
    FIND(fmod.dsp_at, DspAt, "FMOD_ChannelGroup_GetDSP");
    FIND(fmod.dsp_type, DspType, "FMOD_DSP_GetType");
    FIND(fmod.dsp_get_int, DspGetInt, "FMOD_DSP_GetParameterInt");
    FIND(fmod.create_dsp, CreateDsp, "FMOD_System_CreateDSPByType");
    FIND(fmod.dsp_set_int, DspSetInt, "FMOD_DSP_SetParameterInt");
    FIND(fmod.dsp_set_float, DspSetFloat, "FMOD_DSP_SetParameterFloat");
    FIND(fmod.add_dsp, AddDsp, "FMOD_ChannelGroup_AddDSP");
    FIND(fmod.dsp_get_float, DspGetFloat, "FMOD_DSP_GetParameterFloat");
    FIND(fmod.add_fade, AddFade, "FMOD_Channel_AddFadePoint");
#undef FIND
    char line[200];
    if (missing) {
        snprintf(line, sizeof(line), "clock: %s not found; the game plays the shot", missing);
        note(line);
        return;
    }
    HMODULE modules[1024];
    DWORD bytes = 0;
    int patched = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytes))
        for (DWORD i = 0; i < bytes / sizeof(HMODULE); i++) {
            patched += patch_imports(modules[i], "FMOD_Studio_System_Update", update_c, &original_c);
            patched += patch_imports(modules[i], "?update@System@Studio@FMOD@@QEAA?AW4FMOD_RESULT@@XZ", update_cpp,
                                     &original_cpp);
        }
    snprintf(line, sizeof(line), "clock: %d FMOD update import(s) hooked", patched);
    note(line);
}

void m4_clock_trigger(int held) {
    InterlockedExchange(&trigger_held, held);
}

// Queues name, a string that lives as long as the plugin, for the next FMOD update.
void m4_clock_one_shot(const char *name, long volume_percent) {
    LONG at = InterlockedIncrement(&one_shot_head) - 1;
    InterlockedExchange(&one_shot_volumes[at % ONE_SHOTS], volume_percent);
    InterlockedExchangePointer((void *volatile *)&one_shot_queue[at % ONE_SHOTS], (void *)name);
}

int m4_clock_shot(long period_ms, long volume_percent) {
    if (!fmod.group) return 0;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    InterlockedExchange(&beat_ms, period_ms);
    InterlockedExchange(&volume, volume_percent);
    InterlockedExchange64(&last_shot_tick, now.QuadPart);
    InterlockedIncrement(&shots_total);
    return 1;
}
