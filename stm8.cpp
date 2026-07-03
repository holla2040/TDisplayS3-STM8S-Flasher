#include <Arduino.h>
#include "swim.h"
#include "stm8.h"

// SWIM / debug-module registers (same addresses across the family, UM0470)
#define SWIM_CSR      0x7F80
#define DM_CSR2       0x7F99
#define CSR_SAFE_MASK 0x80    // mask internal reset sources
#define CSR_SWIM_DM   0x20    // whole memory map visible over SWIM
#define DM_CSR2_STALL 0x08

// FLASH_IAPSR bits (RM0016)
#define IAPSR_PUL     0x02
#define IAPSR_EOP     0x04

// FLASH_CR2 program bit
#define CR2_PRG       0x01

// Device table — add entries from stm8flash stm8.c as new targets appear.
const stm8_device_t STM8S003 = {
  "STM8S003F3", 0x8000, 8 * 1024, 64,
  0x5062, 0x5064, 0x505F, 0x505B, 0x505C, true
};
const stm8_device_t STM8S103 = {
  "STM8S103F3", 0x8000, 8 * 1024, 64,
  0x5062, 0x5064, 0x505F, 0x505B, 0x505C, true
};
const stm8_device_t STM8S105 = {
  "STM8S105C6", 0x8000, 32 * 1024, 128,
  0x5062, 0x5064, 0x505F, 0x505B, 0x505C, true
};

static bool write_reg(uint16_t addr, uint8_t val) {
  return swim_wotf(addr, &val, 1);
}

// 3 attempts before giving up: a single flaky entry or un-acked frame must
// not read as "no target" — the target is offline only if all three fail.
bool stm8_connect() {
  for (int attempt = 1; attempt <= 3; attempt++) {
    if (attempt > 1) Serial.printf("stm8: connect retry %d/3\n", attempt);
    swim_reset_target(true);
    delay(2);
    if (!swim_entry()) { swim_reset_target(false); continue; }
    if (!write_reg(SWIM_CSR, CSR_SAFE_MASK | CSR_SWIM_DM)) {
      Serial.println("stm8: SWIM_CSR write failed (entry ok, frames not acked)");
      swim_reset_target(false);
      continue;
    }
    swim_reset_target(false);
    delay(2);
    if (!write_reg(DM_CSR2, DM_CSR2_STALL)) {  // halt the core before touching flash
      Serial.println("stm8: CPU stall (DM_CSR2) write failed");
      continue;
    }
    Serial.println("stm8: connected, core stalled");
    return true;
  }
  return false;
}

// 96-bit unique ID at 0x4865 — documented on the STM8S103, absent from the
// STM8S003 datasheet. Blank or unreadable = 003-class. Heuristic only: the
// parts are the same die, so many 003s return a valid UID too.
bool stm8_uid_present(bool verbose) {
  uint8_t uid[12];
  if (!swim_rotf(0x4865, uid, sizeof(uid))) return false;
  bool all0 = true, allF = true;
  for (size_t i = 0; i < sizeof(uid); i++) {
    all0 &= (uid[i] == 0x00);
    allF &= (uid[i] == 0xFF);
  }
  if (verbose) {
    Serial.print("stm8: uid ");
    for (size_t i = 0; i < sizeof(uid); i++) Serial.printf("%02X", uid[i]);
    Serial.printf(" -> %s\n", (all0 || allF) ? "blank (003-class)" : "present (103-class)");
  }
  return !(all0 || allF);
}

static bool unlock_flash(const stm8_device_t *dev) {
  if (!write_reg(dev->regs_pukr, 0x56)) return false;
  if (!write_reg(dev->regs_pukr, 0xAE)) return false;
  uint8_t iapsr = 0;
  if (!swim_rotf(dev->regs_iapsr, &iapsr, 1)) return false;
  if (!(iapsr & IAPSR_PUL)) {
    Serial.printf("stm8: flash unlock rejected, IAPSR=0x%02X\n", iapsr);
    return false;
  }
  return true;
}

static bool wait_eop(const stm8_device_t *dev) {
  uint32_t start = millis();
  uint8_t iapsr = 0;
  while (millis() - start < 20) {
    if (!swim_rotf(dev->regs_iapsr, &iapsr, 1)) return false;
    if (iapsr & IAPSR_EOP) return true;
  }
  Serial.printf("stm8: EOP timeout, IAPSR=0x%02X\n", iapsr);
  return false;
}

// Program the image at flash_start, one block at a time. The final partial
// block is padded with 0xFF (erased state).
bool stm8_program(const stm8_device_t *dev, const uint8_t *image, size_t len,
                  stm8_progress_t progress) {
  if (len > dev->flash_size) return false;
  if (!unlock_flash(dev)) return false;

  Serial.printf("stm8: programming %u bytes in %u-byte blocks\n",
                (unsigned)len, dev->block_size);
  uint8_t block[128];  // max block size in the family
  for (size_t off = 0; off < len; off += dev->block_size) {
    size_t n = min((size_t)dev->block_size, len - off);
    memcpy(block, image + off, n);
    memset(block + n, 0xFF, dev->block_size - n);

    if (!write_reg(dev->regs_cr2, CR2_PRG)) return false;
    if (dev->has_ncr2 && !write_reg(dev->regs_ncr2, (uint8_t)~CR2_PRG)) return false;
    if (!swim_wotf(dev->flash_start + off, block, dev->block_size)) return false;
    if (!wait_eop(dev)) {
      Serial.printf("stm8: block @ 0x%06lX failed\n", dev->flash_start + off);
      return false;
    }
    if (progress) progress(min(off + dev->block_size, len), len);
  }
  Serial.println("stm8: program complete");
  return true;
}

int stm8_verify(const stm8_device_t *dev, const uint8_t *image, size_t len,
                stm8_progress_t progress) {
  uint8_t buf[128];
  for (size_t off = 0; off < len; off += sizeof(buf)) {
    size_t n = min(sizeof(buf), len - off);
    if (!swim_rotf(dev->flash_start + off, buf, n)) return -1;
    if (progress) progress(off + n, len);
    for (size_t i = 0; i < n; i++) {
      if (buf[i] != image[off + i]) {
        Serial.printf("stm8: verify mismatch @ 0x%06lX: wrote 0x%02X read 0x%02X\n",
                      dev->flash_start + off + i, image[off + i], buf[i]);
        return 0;
      }
    }
  }
  return 1;
}

void stm8_run() {
  swim_srst();  // SWIM system reset: target reboots into the new firmware
}
