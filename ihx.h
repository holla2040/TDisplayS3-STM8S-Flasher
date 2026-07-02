#pragma once
#include <stdint.h>
#include <stddef.h>

// Parse Intel HEX text into a flat image buffer. Addresses in the file are
// taken relative to `base` (e.g. 0x8000 for STM8 flash). Gaps stay 0xFF.
// Returns image length (highest address written + 1 - base), or 0 on error.
size_t ihx_parse(const char *text, uint32_t base, uint8_t *image, size_t image_max);
