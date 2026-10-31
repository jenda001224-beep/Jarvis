#include "textfold.h"

#include <stdint.h>

typedef struct {
    uint16_t code;
    char plain;
} accent_t;

static const accent_t ACCENTS[] = {
    {0x00E1, 'a'}, {0x00C1, 'a'}, {0x010D, 'c'}, {0x010C, 'c'}, {0x010F, 'd'}, {0x010E, 'd'}, {0x00E9, 'e'}, {0x00C9, 'e'},
    {0x011B, 'e'}, {0x011A, 'e'}, {0x00ED, 'i'}, {0x00CD, 'i'}, {0x0148, 'n'}, {0x0147, 'n'}, {0x00F3, 'o'}, {0x00D3, 'o'},
    {0x0159, 'r'}, {0x0158, 'r'}, {0x0161, 's'}, {0x0160, 's'}, {0x0165, 't'}, {0x0164, 't'}, {0x00FA, 'u'}, {0x00DA, 'u'},
    {0x016F, 'u'}, {0x016E, 'u'}, {0x00FD, 'y'}, {0x00DD, 'y'}, {0x017E, 'z'}, {0x017D, 'z'}, {0x00E4, 'a'}, {0x00C4, 'a'},
    {0x00F6, 'o'}, {0x00D6, 'o'}, {0x00FC, 'u'}, {0x00DC, 'u'}, {0x013E, 'l'}, {0x013D, 'l'}, {0x013A, 'l'}, {0x0139, 'l'},
    {0x0155, 'r'}, {0x0154, 'r'}, {0x00F4, 'o'}, {0x00D4, 'o'},
};

static char plain_letter(uint16_t code)
{
    for (size_t i = 0; i < sizeof(ACCENTS) / sizeof(ACCENTS[0]); i++) {
        if (ACCENTS[i].code == code) {
            return ACCENTS[i].plain;
        }
    }
    return 0;
}

size_t text_fold(const char *text, char *out, size_t capacity)
{
    size_t length = 0;
    const unsigned char *c = (const unsigned char *) text;
    while (*c && length + 1 < capacity) {
        char plain = ' ';
        if (*c < 0x80) {
            plain = *c >= 'A' && *c <= 'Z' ? (char) (*c + 32) : (char) *c;
            c++;
        } else if ((*c & 0xE0) == 0xC0 && (c[1] & 0xC0) == 0x80) {
            char letter = plain_letter((uint16_t) (((*c & 0x1F) << 6) | (c[1] & 0x3F)));
            plain = letter ? letter : ' ';
            c += 2;
        } else {
            c++;
            while ((*c & 0xC0) == 0x80) {
                c++;
            }
        }
        out[length++] = plain;
    }
    out[length] = 0;
    return length;
}
