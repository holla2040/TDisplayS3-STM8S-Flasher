#pragma once
#include <stdint.h>
#include <stddef.h>

// STM8 device description. Register addresses and block sizes are
// family-specific — values copied from stm8flash's stm8.c device table.
typedef struct {
  const char *name;
  uint32_t flash_start;
  uint32_t flash_size;
  uint16_t block_size;
  uint16_t regs_pukr, regs_dukr, regs_iapsr, regs_cr2, regs_ncr2;
  bool has_ncr2;             // STM8S needs the complement register, STM8L doesn't
} stm8_device_t;

extern const stm8_device_t STM8S003;
extern const stm8_device_t STM8S103;
extern const stm8_device_t STM8S105;

// Called after each block/chunk with (bytes done, total) — for progress UI.
typedef void (*stm8_progress_t)(size_t done, size_t total);

// Full cycle building blocks. All return false on failure.
bool stm8_connect();                                   // reset, SWIM entry, stall CPU
bool stm8_uid_present(bool verbose = true);            // 96-bit UID readable: 103-class die
bool stm8_program(const stm8_device_t *dev, const uint8_t *image, size_t len,
                  stm8_progress_t progress = nullptr);
// 1 = match, 0 = mismatch, -1 = communication error (couldn't read back)
int stm8_verify(const stm8_device_t *dev, const uint8_t *image, size_t len,
                stm8_progress_t progress = nullptr);
void stm8_run();                                       // reset target and let it run
