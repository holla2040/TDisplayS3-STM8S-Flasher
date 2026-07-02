#include <string.h>
#include "ihx.h"

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static int hex_byte(const char *p) {
  int hi = hex_nibble(p[0]), lo = hex_nibble(p[1]);
  return (hi < 0 || lo < 0) ? -1 : (hi << 4) | lo;
}

size_t ihx_parse(const char *text, uint32_t base, uint8_t *image, size_t image_max) {
  memset(image, 0xFF, image_max);
  uint32_t ext = 0;     // extended linear address (record type 04) << 16
  size_t len = 0;

  for (const char *p = text; *p; ) {
    while (*p && *p != ':') p++;
    if (!*p) break;
    p++;

    int count = hex_byte(p);
    int ah = hex_byte(p + 2), al = hex_byte(p + 4);
    int type = hex_byte(p + 6);
    if (count < 0 || ah < 0 || al < 0 || type < 0) return 0;

    uint8_t sum = count + ah + al + type;
    const char *data = p + 8;
    if (type == 0x00) {
      uint32_t addr = ext | (ah << 8) | al;
      for (int i = 0; i < count; i++) {
        int b = hex_byte(data + i * 2);
        if (b < 0) return 0;
        sum += b;
        if (addr + i < base || addr + i - base >= image_max) return 0;
        image[addr + i - base] = b;
        if (addr + i - base + 1 > len) len = addr + i - base + 1;
      }
    } else if (type == 0x04 && count == 2) {
      int b0 = hex_byte(data), b1 = hex_byte(data + 2);
      if (b0 < 0 || b1 < 0) return 0;
      sum += b0 + b1;
      ext = ((uint32_t)b0 << 24) | ((uint32_t)b1 << 16);
    } else if (type == 0x01) {
      return len;   // EOF record
    } else {
      for (int i = 0; i < count; i++) {   // other types: consume for checksum
        int b = hex_byte(data + i * 2);
        if (b < 0) return 0;
        sum += b;
      }
    }

    int cksum = hex_byte(data + count * 2);
    if (cksum < 0 || (uint8_t)(sum + cksum) != 0) return 0;
    p = data + count * 2 + 2;
  }
  return len;   // no EOF record — tolerate, some tools omit it
}
