#include "m4_space.c"
#include <stdlib.h>

#define require(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

static unsigned char ctrl[0x40], actor[0x400], expansion[0x80], item[0x80], record[0x30];
static unsigned calls;
static unsigned original_value;
static int cut;

void note(const char *text) { puts(text); }

static unsigned char original(unsigned char *value) {
    require(value == ctrl);
    calls++;
    return original_value;
}

static unsigned char *fake_expand(void *value) {
    require(value == actor);
    return cut == 3 ? NULL : expansion;
}

static unsigned char *fake_record(void *value) {
    require(value == item);
    return cut == 5 ? NULL : record;
}

static const struct {
    const char *name;
    int weapon;
    int original;
    int cut;
    int expected;
    unsigned calls;
} space_cases[] = {
    {"player M4", 1, 1, 0, 0, 0},
    {"other weapon false original", 0, 0, 0, 0, 1},
    {"other weapon true original", 0, 1, 0, 1, 1},
    {"null actor", 1, 0, 1, 0, 1},
    {"null actor slot", 1, 0, 2, 0, 1},
    {"null expansion", 1, 0, 3, 0, 1},
    {"null item", 1, 0, 4, 0, 1},
    {"null record", 1, 0, 5, 0, 1},
};

int main(void) {
    unsigned char *game = VirtualAlloc(NULL, 0x4000000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    require(game);
    for (size_t i = 0; i < sizeof(space_signatures) / sizeof(space_signatures[0]); i++)
        memcpy(game + space_signatures[i].rva, space_signatures[i].bytes, space_signatures[i].length);
    size_t sites[] = {SPACE_DROP_CALL_RVA, SPACE_PRESS_CALL_RVA, SPACE_REARM_CALL_RVA};
    const unsigned char *site_bytes[] = {SPACE_DROP_CALL_BYTES, SPACE_PRESS_CALL_BYTES, SPACE_REARM_CALL_BYTES};
    for (size_t i = 0; i < sizeof(space_signatures) / sizeof(space_signatures[0]); i++) {
        game[space_signatures[i].rva] ^= 1;
        require(!space_install(game));
        game[space_signatures[i].rva] ^= 1;
        for (size_t j = 0; j < sizeof(sites) / sizeof(sites[0]); j++)
            require(!memcmp(game + sites[j], site_bytes[j], sizeof(SPACE_DROP_CALL_BYTES)));
    }
    puts("PASS: six signature failures install no space hooks and leave every call site alone");

    require(space_install(game));
    unsigned char *entry = NULL;
    for (size_t j = 0; j < sizeof(sites) / sizeof(sites[0]); j++) {
        unsigned char *site = game + sites[j];
        require(site[0] == 0xe8);
        if (!entry) entry = site + 5 + *(INT32 *)(site + 1);
        else require(site + 5 + *(INT32 *)(site + 1) == entry);
    }
    static const unsigned char JUMP[] = {0xff, 0x25, 0x00, 0x00, 0x00, 0x00};
    require(entry && !memcmp(entry, JUMP, sizeof(JUMP)));
    require(*(void **)(entry + 6) == (void *)space_blocked);
    puts("PASS: all three call sites decode to one entry page that jumps to the space test");

    *(void **)(ctrl + 0x18) = actor;
    space_original = original;
    space_expand = fake_expand;
    space_record = fake_record;
    const unsigned char enter[] = {0x48, 0x83, 0xec, 0x28};
    const unsigned char leave[] = {0x48, 0x83, 0xc4, 0x28, 0xc3};
    const unsigned char m4_guid[] = {0x19, 0x35, 0x6b, 0x10, 0x07, 0x06, 0x29, 0x4b, 0xb7, 0xa2, 0x9d, 0x8f, 0xb5, 0x54, 0xa2, 0x3e};
    const unsigned char other_guid[] = {0x19, 0x35, 0x6b, 0x10, 0x07, 0x06, 0x29, 0x4b, 0xb7, 0xa2, 0x9d, 0x8f, 0xb5, 0x54, 0xa2, 0x3f};
    for (size_t j = 0; j < sizeof(sites) / sizeof(sites[0]); j++) {
        unsigned char *site = game + sites[j];
        memcpy(site - sizeof(enter), enter, sizeof(enter));
        memcpy(site + 7, leave, sizeof(leave));
        int (*bridge)(unsigned char *) = (void *)(site - sizeof(enter));
        for (size_t i = 0; i < sizeof(space_cases) / sizeof(space_cases[0]); i++) {
            cut = space_cases[i].cut;
            calls = 0;
            original_value = space_cases[i].original;
            *(void **)(ctrl + 0x18) = cut == 1 ? NULL : actor;
            *(void **)(actor + 0x280) = cut == 2 ? NULL : actor;
            *(void **)(expansion + 0x40) = cut == 4 ? NULL : item;
            memcpy(record + 8, space_cases[i].weapon ? m4_guid : other_guid, sizeof(m4_guid));
            int result = bridge(ctrl);
            if ((unsigned char)result != space_cases[i].expected || calls != space_cases[i].calls) {
                fprintf(stderr, "FAIL: %s returned %d with %u original calls, want %d and %u\n",
                        space_cases[i].name, (unsigned char)result, calls, space_cases[i].expected, space_cases[i].calls);
                exit(1);
            }
        }
    }
    puts("PASS: player M4 bypasses, other weapons and every broken chain fall through to the original on all three call sites");
    VirtualFree(game, 0, MEM_RELEASE);
    return 0;
}
