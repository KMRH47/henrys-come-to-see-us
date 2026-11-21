#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "m4_find_table.h"

void note(const char *text);

enum { FIND_UNSET, FIND_WORKING, FIND_FIXED, FIND_FOUND };

static volatile LONG find_state;
static unsigned find_where[M4_FIND_COUNT];
static unsigned find_seen[M4_FIND_COUNT];
static unsigned char *find_first[M4_FIND_COUNT];
static unsigned short find_heads[0x10000];
static unsigned short find_next[M4_FIND_COUNT + 1];
static unsigned char find_nowhere[0x20000];

static int find_skipped(const struct m4_find_row *row, size_t at) {
    return row->skip[at >> 3] >> (at & 7) & 1;
}

static int find_matches(const unsigned char *game, const unsigned char *at, const struct m4_find_row *row) {
    for (size_t i = 0; i < row->length; i++)
        if (!find_skipped(row, i) && at[i] != row->bytes[i]) return 0;
    if (!row->aim) return 1;
    return find_where[row->target] && at + row->aim + 4 + *(INT32 *)(at + row->aim) == game + find_where[row->target];
}

static void find_pass(unsigned char *game, IMAGE_NT_HEADERS64 *header, int aimed) {
    memset(find_heads, 0, sizeof(find_heads));
    for (unsigned i = 0; i < M4_FIND_COUNT; i++) {
        const struct m4_find_row *row = &M4_FIND_ROWS[i];
        if (!row->aim != !aimed) continue;
        unsigned key = row->bytes[row->anchor] | row->bytes[row->anchor + 1] << 8;
        find_next[i + 1] = find_heads[key];
        find_heads[key] = (unsigned short)(i + 1);
    }
    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(header);
    for (unsigned s = 0; s < header->FileHeader.NumberOfSections; s++) {
        if (!(section[s].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        unsigned char *low = game + section[s].VirtualAddress, *high = low + section[s].Misc.VirtualSize;
        for (unsigned char *at = low; at + 1 < high; at++) {
            for (unsigned short link = find_heads[at[0] | at[1] << 8]; link; link = find_next[link]) {
                const struct m4_find_row *row = &M4_FIND_ROWS[link - 1];
                unsigned char *start = at - row->anchor;
                if (start < low || start + row->length > high || !find_matches(game, start, row)) continue;
                if (!find_seen[link - 1]++) find_first[link - 1] = start;
            }
        }
    }
    for (unsigned i = 0; i < M4_FIND_COUNT; i++) {
        const struct m4_find_row *row = &M4_FIND_ROWS[i];
        if (!row->aim != !aimed || find_seen[i] != 1) continue;
        unsigned char *spot = find_first[i] + row->offset;
        if (row->read) spot = find_first[i] + row->read + 4 + *(INT32 *)(find_first[i] + row->read) + row->add;
        if (spot > game && spot < game + header->OptionalHeader.SizeOfImage) find_where[i] = (unsigned)(spot - game);
    }
}

static void find_all(unsigned char *game, IMAGE_NT_HEADERS64 *header) {
    char line[160];
    unsigned missing = 0;
    find_pass(game, header, 0);
    find_pass(game, header, 1);
    for (unsigned i = 0; i < M4_FIND_COUNT; i++) {
        if (find_where[i]) continue;
        missing++;
        snprintf(line, sizeof(line), "find: %s matches %u places in this WHGame.dll", M4_FIND_ROWS[i].name, find_seen[i]);
        note(line);
    }
    if (missing) memset(find_where, 0, sizeof(find_where));
    if (missing) snprintf(line, sizeof(line), "find: this WHGame.dll is not the Steam build; %u of %u game addresses not found, every hook stays off",
                          missing, (unsigned)M4_FIND_COUNT);
    else snprintf(line, sizeof(line), "find: this WHGame.dll is not the Steam build; all %u game addresses found by their bytes", (unsigned)M4_FIND_COUNT);
    note(line);
}

static IMAGE_NT_HEADERS64 *find_header(unsigned char *game) {
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)game;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    IMAGE_NT_HEADERS64 *header = (IMAGE_NT_HEADERS64 *)(game + dos->e_lfanew);
    return header->Signature == IMAGE_NT_SIGNATURE ? header : NULL;
}

static LONG find_ready(unsigned char *game) {
    LONG state = InterlockedCompareExchange(&find_state, FIND_WORKING, FIND_UNSET);
    if (state == FIND_UNSET) {
        IMAGE_NT_HEADERS64 *header = find_header(game);
        state = !header || (header->FileHeader.TimeDateStamp == M4_FIND_STEAM_STAMP &&
                            header->OptionalHeader.SizeOfImage == M4_FIND_STEAM_SIZE) ? FIND_FIXED : FIND_FOUND;
        if (state == FIND_FOUND) find_all(game, header);
        InterlockedExchange(&find_state, state);
    }
    while (state == FIND_WORKING) {
        Sleep(1);
        state = InterlockedCompareExchange(&find_state, 0, 0);
    }
    return state;
}

unsigned char *m4_at(unsigned char *game, size_t steam) {
    if (find_ready(game) == FIND_FIXED) return game + steam;
    unsigned low = 0, high = M4_FIND_COUNT;
    while (low < high) {
        unsigned middle = (low + high) / 2;
        if (M4_FIND_ROWS[middle].steam < steam) low = middle + 1;
        else high = middle;
    }
    if (low == M4_FIND_COUNT || M4_FIND_ROWS[low].steam != steam || !find_where[low]) return find_nowhere + sizeof(find_nowhere) / 2;
    return game + find_where[low];
}

int m4_differs(unsigned char *game, size_t steam, const unsigned char *bytes, size_t length) {
    if (find_ready(game) == FIND_FIXED) return memcmp(game + steam, bytes, length) != 0;
    for (unsigned i = 0; i < M4_FIND_COUNT; i++) {
        const struct m4_find_row *row = &M4_FIND_ROWS[i];
        size_t first = row->steam - row->offset;
        if (row->read || !find_where[i] || steam < first || steam + length > first + row->length) continue;
        const unsigned char *at = game + find_where[i] - row->offset;
        for (size_t j = 0; j < length; j++)
            if (!find_skipped(row, steam - first + j) && at[steam - first + j] != bytes[j]) return 1;
        return 0;
    }
    return 1;
}
