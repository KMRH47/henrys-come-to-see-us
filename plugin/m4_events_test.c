#include "m4_events.c"
#include "m4_patch.c"
#include <stdlib.h>

#define require(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

#define MAX_NODES 1100
#define MAX_NAMES 1200

struct fake_node {
    void **vtable;
    const char *name;
    int events;
};

struct fake_file {
    const char *path;
    int readable;
    int count;
    struct fake_node nodes[MAX_NODES];
};

struct fake_list {
    char name[32];
    const char *source;
    int events;
    int loads;
};

struct bridge_out {
    unsigned r14;
    unsigned char zero;
    unsigned char pad[3];
    void *rcx;
};

static void *fake_vtable[64];
static struct fake_file files[3];
static struct fake_list lists[MAX_NAMES];
static int list_count;
static int mod_loads;
static int attribute_calls;
static void (*bridge)(void *, int, struct bridge_out *);
static unsigned char *game_base;

void note(const char *text) {
    if (!strstr(text, "mismatch")) puts(text);
}

static const char *fake_attribute(void *self, const char *key) {
    require(!strcmp(key, "name"));
    attribute_calls++;
    return ((struct fake_node *)self)->name;
}

static struct fake_list *list_of(const char *name) {
    for (int i = 0; i < list_count; i++)
        if (!strcmp(lists[i].name, name)) return &lists[i];
    require(list_count < MAX_NAMES && strlen(name) < sizeof(lists[0].name));
    struct fake_list *list = &lists[list_count++];
    strcpy(list->name, name);
    list->source = "none";
    return list;
}

static int same_path(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (events_fold(*a) != events_fold(*b)) return 0;
    return !*a && !*b;
}

static unsigned char fake_loader(void *skeleton, const char *path) {
    require(skeleton == files);
    for (size_t f = 0; f < sizeof(files) / sizeof(files[0]); f++) {
        if (!same_path(files[f].path, path)) continue;
        if (f == 0) mod_loads++;
        if (!files[f].readable) return 0;
        for (int i = 0; i < files[f].count; i++) {
            struct fake_node *node = &files[f].nodes[i];
            struct bridge_out out;
            bridge(node, node->events, &out);
            require(out.rcx == (void *)node && out.zero == (out.r14 == 0));
            if (!out.r14) continue;
            struct fake_list *list = list_of(node->name);
            if (list->events) continue;
            list->source = files[f].path;
            list->events = node->events;
            list->loads++;
        }
        return 1;
    }
    return 0;
}

static void reset_files(void) {
    memset(files, 0, sizeof(files));
    memset(lists, 0, sizeof(lists));
    list_count = 0;
    mod_loads = 0;
    files[0].path = EVENTS_MOD_PATH;
    files[0].readable = 1;
    files[1].path = "animations\\humans\\male\\male.animevents";
    files[1].readable = 1;
    files[2].path = "animations/humans/female/female.animevents";
    files[2].readable = 1;
}

static void add_node(int file, const char *name, int events) {
    require(files[file].count < MAX_NODES);
    files[file].nodes[files[file].count++] = (struct fake_node){fake_vtable, name, events};
}

static void fill_standard(void) {
    reset_files();
    add_node(0, "clip_a", 1);
    add_node(0, "clip_b", 0);
    add_node(0, "clip_new", 2);
    add_node(0, "clip_new_empty", 0);
    add_node(1, "clip_a", 3);
    add_node(1, "clip_b", 2);
    add_node(1, "clip_c", 1);
    add_node(1, "clip_d", 4);
    add_node(1, "clip_e", 0);
    add_node(1, "clip_new", 9);
    add_node(2, "clip_a", 7);
    add_node(2, "clip_c", 5);
    list_of("clip_a");
    list_of("clip_b");
    list_of("clip_c");
    list_of("clip_d");
    list_of("clip_e");
    list_of("clip_new");
    list_of("clip_new_empty");
}

static const char *const MALE = "animations\\humans\\male\\male.animevents";

static const struct {
    const char *name;
    const char *paths[3];
    int unreadable_mod;
    int expected_mod_loads;
    struct { const char *clip; const char *source; int events; } expected[7];
} flow_cases[] = {
    {"male merges the mod over vanilla", {"animations\\humans\\male\\male.animevents"}, 0, 1, {
        {"clip_a", EVENTS_MOD_PATH, 1}, {"clip_b", "none", 0}, {"clip_c", "animations\\humans\\male\\male.animevents", 1},
        {"clip_d", "animations\\humans\\male\\male.animevents", 4}, {"clip_e", "none", 0},
        {"clip_new", EVENTS_MOD_PATH, 2}, {"clip_new_empty", "none", 0}}},
    {"second male load changes nothing", {"animations\\humans\\male\\male.animevents", "animations\\humans\\male\\male.animevents"}, 0, 2, {
        {"clip_a", EVENTS_MOD_PATH, 1}, {"clip_b", "none", 0}, {"clip_c", "animations\\humans\\male\\male.animevents", 1},
        {"clip_d", "animations\\humans\\male\\male.animevents", 4}, {"clip_e", "none", 0},
        {"clip_new", EVENTS_MOD_PATH, 2}, {"clip_new_empty", "none", 0}}},
    {"upper case slash path", {"Animations/Humans/Male/MALE.ANIMEVENTS"}, 0, 1, {
        {"clip_a", EVENTS_MOD_PATH, 1}, {"clip_b", "none", 0}, {"clip_c", "animations\\humans\\male\\male.animevents", 1},
        {"clip_d", "animations\\humans\\male\\male.animevents", 4}, {"clip_e", "none", 0},
        {"clip_new", EVENTS_MOD_PATH, 2}, {"clip_new_empty", "none", 0}}},
    {"female never sees the mod", {"animations/humans/female/female.animevents"}, 0, 0, {
        {"clip_a", "animations/humans/female/female.animevents", 7}, {"clip_b", "none", 0},
        {"clip_c", "animations/humans/female/female.animevents", 5}, {"clip_d", "none", 0}, {"clip_e", "none", 0},
        {"clip_new", "none", 0}, {"clip_new_empty", "none", 0}}},
    {"mod file missing leaves vanilla whole", {"animations\\humans\\male\\male.animevents"}, 1, 1, {
        {"clip_a", "animations\\humans\\male\\male.animevents", 3}, {"clip_b", "animations\\humans\\male\\male.animevents", 2},
        {"clip_c", "animations\\humans\\male\\male.animevents", 1}, {"clip_d", "animations\\humans\\male\\male.animevents", 4},
        {"clip_e", "none", 0}, {"clip_new", "animations\\humans\\male\\male.animevents", 9}, {"clip_new_empty", "none", 0}}},
    {"female then male", {"animations/humans/female/female.animevents", "animations\\humans\\male\\male.animevents"}, 0, 1, {
        {"clip_a", "animations/humans/female/female.animevents", 7}, {"clip_b", "none", 0},
        {"clip_c", "animations/humans/female/female.animevents", 5}, {"clip_d", "animations\\humans\\male\\male.animevents", 4},
        {"clip_e", "none", 0}, {"clip_new", EVENTS_MOD_PATH, 2}, {"clip_new_empty", "none", 0}}},
};

static const struct { const char *path; int male; } path_cases[] = {
    {"animations\\humans\\male\\male.animevents", 1},
    {"Animations/Humans/Male/Male.AnimEvents", 1},
    {"humans/male/male.animevents", 1},
    {"d:\\data\\animations\\humans\\male\\male.animevents", 1},
    {"animations/humans/female/female.animevents", 0},
    {"animations\\humans\\female\\female.animevents", 0},
    {"animations/assets/firearms/m4a1.animevents", 0},
    {"animations/assets/firearms/hand_cannon.animevents", 0},
    {"animations/humans/male/male.animevents.bak", 0},
    {"male.animevents", 0},
    {"", 0},
    {NULL, 0},
};

static const struct { const char *name; LONG phase; DWORD owner_other; const char *node; int count; int claimed; int expected; } node_cases[] = {
    {"no pass", 0, 0, "clip_a", 3, 0, 3},
    {"mod pass keeps the count", 1, 0, "clip_a", 3, 0, 3},
    {"mod pass keeps an empty count", 1, 0, "clip_b", 0, 0, 0},
    {"vanilla pass keeps an unclaimed count", 2, 0, "clip_c", 4, 0, 4},
    {"vanilla pass skips a claimed node", 2, 0, "clip_a", 4, 1, 0},
    {"vanilla pass skips a claimed node case blind", 2, 0, "CLIP_A", 4, 1, 0},
    {"vanilla pass keeps a claimed empty node", 2, 0, "clip_b", 0, 1, 0},
    {"another thread is left alone", 2, 1, "clip_a", 4, 1, 4},
};

static unsigned char *decode_call(unsigned char *site) {
    require(site[0] == 0xe8);
    return site + 5 + *(INT32 *)(site + 1);
}

static void make_bridge(unsigned char *at, void *stub) {
    static const unsigned char HEAD[] = {
        0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x28, 0x4d, 0x89, 0xc7, 0x41, 0xbe, 0x55, 0x55, 0x55, 0x55, 0x49, 0xbb
    };
    static const unsigned char TAIL[] = {
        0x89, 0xd0, 0x41, 0xff, 0xd3, 0x45, 0x89, 0x37, 0x41, 0x0f, 0x94, 0x47, 0x04, 0x49, 0x89, 0x4f, 0x08,
        0x48, 0x83, 0xc4, 0x28, 0x41, 0x5f, 0x41, 0x5e, 0xc3
    };
    at = emit(at, HEAD, sizeof(HEAD));
    at = emit(at, &stub, sizeof(stub));
    emit(at, TAIL, sizeof(TAIL));
}

int main(void) {
    fake_vtable[EVENTS_NODE_ATTR_SLOT] = (void *)fake_attribute;
    game_base = VirtualAlloc(NULL, 0x4000000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    require(game_base);
    unsigned char *game = game_base;
    for (size_t i = 0; i < sizeof(events_signatures) / sizeof(events_signatures[0]); i++)
        memcpy(game + events_signatures[i].rva, events_signatures[i].bytes, events_signatures[i].length);
    for (size_t i = 0; i < sizeof(events_signatures) / sizeof(events_signatures[0]); i++) {
        for (size_t b = 0; b < events_signatures[i].length; b++) {
            game[events_signatures[i].rva + b] ^= 1;
            require(!events_install(game));
            game[events_signatures[i].rva + b] ^= 1;
        }
    }
    for (size_t i = 0; i < sizeof(events_signatures) / sizeof(events_signatures[0]); i++)
        require(!memcmp(game + events_signatures[i].rva, events_signatures[i].bytes, events_signatures[i].length));
    puts("PASS: every signature byte failing installs no events hooks and leaves every site alone");

    require(events_install(game));
    unsigned char *entry = decode_call(game + EVENTS_CALL_RVA + EVENTS_CALL_OFFSET);
    unsigned char *stub = decode_call(game + EVENTS_NODE_RVA + EVENTS_NODE_OFFSET);
    static const unsigned char JUMP[] = {0xff, 0x25, 0x00, 0x00, 0x00, 0x00};
    require(!memcmp(entry, JUMP, sizeof(JUMP)) && *(void **)(entry + 6) == (void *)events_load);
    require(!memcmp(stub, EVENTS_NODE_STUB_HEAD, sizeof(EVENTS_NODE_STUB_HEAD)));
    require(*(void **)(stub + sizeof(EVENTS_NODE_STUB_HEAD)) == (void *)events_node);
    require(!memcmp(stub + sizeof(EVENTS_NODE_STUB_HEAD) + 8, EVENTS_NODE_STUB_TAIL, sizeof(EVENTS_NODE_STUB_TAIL)));
    require(events_original == (void *)(game + EVENTS_LOADER_RVA));
    unsigned char *node_site = game + EVENTS_NODE_RVA;
    require(!memcmp(node_site, EVENTS_NODE_BYTES, EVENTS_NODE_OFFSET));
    require(!memcmp(node_site + EVENTS_NODE_OFFSET + 5, EVENTS_NODE_BYTES + EVENTS_NODE_OFFSET + 5, sizeof(EVENTS_NODE_BYTES) - EVENTS_NODE_OFFSET - 5));
    puts("PASS: install patches only the two call sites, one to the load entry and one to the node stub");

    unsigned char *bridge_code = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    require(bridge_code);
    make_bridge(bridge_code, stub);
    bridge = (void *)bridge_code;

    for (size_t i = 0; i < sizeof(path_cases) / sizeof(path_cases[0]); i++) {
        if (events_is_male(path_cases[i].path) != path_cases[i].male) {
            fprintf(stderr, "FAIL: path %s male %d, want %d\n", path_cases[i].path ? path_cases[i].path : "(null)",
                    events_is_male(path_cases[i].path), path_cases[i].male);
            exit(1);
        }
    }
    puts("PASS: only the male events path starts a merge");

    for (size_t i = 0; i < sizeof(node_cases) / sizeof(node_cases[0]); i++) {
        events_claimed = 0;
        events_claim(events_hash("clip_a"));
        if (node_cases[i].claimed) events_claim(events_hash("clip_b"));
        events_phase = node_cases[i].phase;
        events_owner = node_cases[i].owner_other ? GetCurrentThreadId() + 1 : GetCurrentThreadId();
        struct fake_node node = {fake_vtable, node_cases[i].node, node_cases[i].count};
        struct bridge_out out;
        bridge(&node, node_cases[i].count, &out);
        if (out.r14 != (unsigned)node_cases[i].expected || out.zero != (node_cases[i].expected == 0) || out.rcx != (void *)&node) {
            fprintf(stderr, "FAIL: %s returned %u zero %d, want %d\n", node_cases[i].name, out.r14, out.zero, node_cases[i].expected);
            exit(1);
        }
    }
    events_phase = 0;
    events_owner = 0;
    puts("PASS: the node stub returns the child count in r14 and the flags, and restores rcx, in every phase");

    for (size_t i = 0; i < sizeof(flow_cases) / sizeof(flow_cases[0]); i++) {
        fill_standard();
        files[0].readable = !flow_cases[i].unreadable_mod;
        events_original = fake_loader;
        attribute_calls = 0;
        for (size_t p = 0; p < 3 && flow_cases[i].paths[p]; p++) {
            unsigned char (*load)(void *, const char *) = (void *)entry;
            require(load(files, flow_cases[i].paths[p]) == 1);
            require(events_phase == 0 && events_owner == 0);
        }
        if (mod_loads != flow_cases[i].expected_mod_loads) {
            fprintf(stderr, "FAIL: %s read the mod file %d times, want %d\n", flow_cases[i].name, mod_loads, flow_cases[i].expected_mod_loads);
            exit(1);
        }
        for (size_t e = 0; e < 7; e++) {
            struct fake_list *list = list_of(flow_cases[i].expected[e].clip);
            if (strcmp(list->source, flow_cases[i].expected[e].source) || list->events != flow_cases[i].expected[e].events || list->loads > 1) {
                fprintf(stderr, "FAIL: %s: %s came from %s with %d events after %d loads, want %s with %d\n", flow_cases[i].name,
                        list->name, list->source, list->events, list->loads, flow_cases[i].expected[e].source, flow_cases[i].expected[e].events);
                exit(1);
            }
        }
        require(attribute_calls > 0 || flow_cases[i].expected_mod_loads == 0);
    }
    puts("PASS: mod animations win, silenced ones stay empty, the rest stay vanilla, in every load order");

    fill_standard();
    for (int i = 0; i < EVENTS_CLAIM_LIMIT + 20; i++) {
        static char names[EVENTS_CLAIM_LIMIT + 20][16];
        snprintf(names[i], sizeof(names[i]), "bulk_%d", i);
        add_node(0, names[i], 0);
        add_node(1, names[i], 1);
        list_of(names[i]);
    }
    events_original = fake_loader;
    require(((unsigned char (*)(void *, const char *))entry)(files, MALE) == 1);
    require(events_claimed == EVENTS_CLAIM_LIMIT && events_dropped >= 20);
    require(list_of("bulk_0")->events == 0 && list_of("bulk_1039")->events == 1);
    require(list_of("clip_a")->events == 1 && list_of("clip_c")->events == 1 && list_of("clip_d")->events == 4);
    puts("PASS: a full claim table drops the surplus claims without touching the rest");

    VirtualFree(bridge_code, 0, MEM_RELEASE);
    VirtualFree(game_base, 0, MEM_RELEASE);
    return 0;
}
