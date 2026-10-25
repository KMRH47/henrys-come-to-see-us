#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

unsigned char *near_page(unsigned char *base) {
    for (ULONG_PTR step = 0x10000; step < 0x70000000; step += 0x10000) {
        void *page = VirtualAlloc(base - step, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (page) return page;
    }
    return NULL;
}

unsigned char *emit(unsigned char *at, const void *bytes, size_t length) {
    memcpy(at, bytes, length);
    return at + length;
}

unsigned char *emit_call(unsigned char *at, void *target) {
    static const unsigned char CALL_NEXT_SLOT[] = {0xff, 0x15, 0x02, 0x00, 0x00, 0x00, 0xeb, 0x08};
    at = emit(at, CALL_NEXT_SLOT, sizeof(CALL_NEXT_SLOT));
    return emit(at, &target, 8);
}

unsigned char *emit_jump(unsigned char *at, void *target) {
    static const unsigned char JUMP_NEXT_SLOT[] = {0xff, 0x25, 0x00, 0x00, 0x00, 0x00};
    at = emit(at, JUMP_NEXT_SLOT, sizeof(JUMP_NEXT_SLOT));
    return emit(at, &target, 8);
}

static void put_branch(unsigned char *from, unsigned char *to, size_t length, unsigned char opcode) {
    DWORD old;
    VirtualProtect(from, length, PAGE_EXECUTE_READWRITE, &old);
    memset(from + 5, 0x90, length - 5);
    *(INT32 *)(from + 1) = (INT32)(to - (from + 5));
    from[0] = opcode;
    VirtualProtect(from, length, old, &old);
    FlushInstructionCache(GetCurrentProcess(), from, length);
}

void put_jump(unsigned char *from, unsigned char *to, size_t length) {
    put_branch(from, to, length, 0xe9);
}

void put_call(unsigned char *from, unsigned char *to, size_t length) {
    put_branch(from, to, length, 0xe8);
}
