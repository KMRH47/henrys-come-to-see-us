#ifndef M4_WEAPON_H
#define M4_WEAPON_H
#include <string.h>

static inline int m4_is_weapon_record(const unsigned char *record) {
    static const unsigned char guid[] = {0x19, 0x35, 0x6b, 0x10, 0x07, 0x06, 0x29, 0x4b, 0xb7, 0xa2, 0x9d, 0x8f, 0xb5, 0x54, 0xa2, 0x3e};
    return record && !memcmp(record + 8, guid, sizeof(guid));
}
#endif
