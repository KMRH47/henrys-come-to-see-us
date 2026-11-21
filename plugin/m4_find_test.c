#include "m4_find.c"
#include <stdlib.h>

#define require(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)
#define SHIFT 0x5000

static unsigned notes;

void note(const char *text) {
    notes++;
    puts(text);
}

static unsigned char *file_bytes;

static void read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    require(file);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    file_bytes = malloc(size);
    require(file_bytes && fread(file_bytes, 1, size, file) == (size_t)size);
    fclose(file);
}

static unsigned char *mapped(unsigned shift, int steam) {
    IMAGE_NT_HEADERS64 *source = find_header(file_bytes);
    require(source);
    unsigned char *game = VirtualAlloc(NULL, source->OptionalHeader.SizeOfImage + shift, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    require(game);
    memcpy(game, file_bytes, source->OptionalHeader.SizeOfHeaders);
    IMAGE_NT_HEADERS64 *header = find_header(game);
    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(header);
    for (unsigned i = 0; i < header->FileHeader.NumberOfSections; i++) {
        DWORD size = section[i].SizeOfRawData < section[i].Misc.VirtualSize ? section[i].SizeOfRawData : section[i].Misc.VirtualSize;
        section[i].VirtualAddress += shift;
        memcpy(game + section[i].VirtualAddress, file_bytes + section[i].PointerToRawData, size);
    }
    header->OptionalHeader.SizeOfImage += shift;
    if (!steam) header->FileHeader.TimeDateStamp ^= 1;
    memset(find_where, 0, sizeof(find_where));
    memset(find_seen, 0, sizeof(find_seen));
    memset(find_first, 0, sizeof(find_first));
    find_state = FIND_UNSET;
    notes = 0;
    return game;
}

static unsigned plain_row(void) {
    for (unsigned i = 0; i < M4_FIND_COUNT; i++)
        if (!M4_FIND_ROWS[i].read && !M4_FIND_ROWS[i].aim && M4_FIND_ROWS[i].length <= 64) return i;
    exit(1);
}

static unsigned literal_at(const struct m4_find_row *row) {
    for (unsigned i = 0; i < row->length; i++)
        if (!find_skipped(row, i)) return i;
    exit(1);
}

static int nowhere(const unsigned char *at) {
    return at >= find_nowhere && at < find_nowhere + sizeof(find_nowhere);
}

static const struct {
    const char *name;
    unsigned shift;
    int steam;
    int broken;
    int found;
} find_cases[] = {
    {"the Steam build keeps its fixed addresses", 0, 1, 0, 1},
    {"another build with the same layout finds every address", 0, 0, 0, 1},
    {"another build with moved code finds every address where it moved", SHIFT, 0, 0, 1},
    {"one changed byte at one address turns every address off", 0, 0, 1, 0},
    {"one pattern seen twice turns every address off", 0, 0, 2, 0},
};

int main(int count, char **arguments) {
    require(count == 2);
    read_file(arguments[1]);
    unsigned plain = plain_row();
    const struct m4_find_row *broken = &M4_FIND_ROWS[plain];
    for (size_t c = 0; c < sizeof(find_cases) / sizeof(find_cases[0]); c++) {
        unsigned shift = find_cases[c].shift;
        unsigned char *game = mapped(shift, find_cases[c].steam);
        unsigned char *site = game + shift + broken->steam - broken->offset;
        if (find_cases[c].broken == 1) site[literal_at(broken)] ^= 1;
        if (find_cases[c].broken == 2) memcpy(game + shift + 0x1000, site, broken->length);
        ULONGLONG before = GetTickCount64();
        m4_at(game, M4_FIND_ROWS[0].steam);
        ULONGLONG took = GetTickCount64() - before;
        require(find_state == (find_cases[c].steam ? FIND_FIXED : FIND_FOUND));
        require(notes == (find_cases[c].steam ? 0u : find_cases[c].found ? 1u : notes) && (find_cases[c].steam || notes));
        if (find_cases[c].broken == 2) require(find_seen[plain] == 2);
        for (unsigned i = 0; i < M4_FIND_COUNT; i++) {
            const struct m4_find_row *row = &M4_FIND_ROWS[i];
            unsigned char *at = m4_at(game, row->steam);
            if (!find_cases[c].found) {
                require(nowhere(at) && nowhere(at - 0x8000) && nowhere(at + 0x8000) && !at[0]);
                require(m4_differs(game, row->steam - row->offset, row->bytes, row->length));
                continue;
            }
            if (at != game + shift + row->steam) fprintf(stderr, "%s is at %#x, not %#x\n", row->name, (unsigned)(at - game), shift + row->steam);
            require(at == game + shift + row->steam);
            if (row->read || find_cases[c].steam) continue;
            require(!m4_differs(game, row->steam - row->offset, row->bytes, row->length));
            unsigned char changed[2048];
            require(row->length <= sizeof(changed));
            memcpy(changed, row->bytes, row->length);
            changed[literal_at(row)] ^= 1;
            require(m4_differs(game, row->steam - row->offset, changed, row->length));
            require(m4_differs(game, row->steam - row->offset - 1, row->bytes, row->length));
        }
        require(nowhere(m4_at(game, 1)) || find_cases[c].steam);
        if (find_cases[c].steam) {
            require(!m4_differs(game, broken->steam, game + broken->steam, 16));
            require(m4_differs(game, broken->steam, game + broken->steam + 1, 16));
        }
        printf("PASS: %s (%llu ms)\n", find_cases[c].name, took);
        VirtualFree(game, 0, MEM_RELEASE);
    }
    return 0;
}
